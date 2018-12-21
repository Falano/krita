/*
 *  Copyright (c) 2008 Cyrille Berger <cberger@cberger.net>
 *
 *  This library is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; version 2.1 of the License.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include "kis_small_color_widget.h"
#include <QTimer>
#include "kis_slider_spin_box.h"
#include <QVBoxLayout>
#include "kis_signal_compressor.h"
#include <ImfRgba.h>

#include <KoColorConversions.h>

#include "kis_debug.h"
#include "kis_assert.h"

#include <KoColor.h>
#include "KisGLImageF16.h"
#include "KisGLImageWidget.h"
#include "KisClickableGLImageWidget.h"
#include "kis_display_color_converter.h"
#include "kis_signal_auto_connection.h"

#include <KoColorModelStandardIds.h>
#include <KoColorSpaceRegistry.h>
#include "kis_fixed_paint_device.h"
#include <opengl/KisOpenGLModeProber.h>


struct KisSmallColorWidget::Private {
    qreal hue; // 0 ... 1.0
    qreal value; // 0 ... 1.0
    qreal saturation; // 0 ... 1.0
    bool updateAllowed;
    QTimer updateTimer;
    KisClickableGLImageWidget *hueWidget;
    KisClickableGLImageWidget *valueWidget;
    KisSignalCompressor *resizeUpdateCompressor;
    KisSignalCompressor *valueSliderUpdateCompressor;
    int huePreferredHeight = 32;
    KisSliderSpinBox *dynamicRange = 0;
    qreal currentRelativeDynamicRange = 1.0;
    KisDisplayColorConverter *displayColorConverter = 0;
    KisSignalAutoConnectionsStore colorConverterConnections;
    bool hasHDR = false;
    bool hasHardwareHDR = false;

    qreal effectiveRelativeDynamicRange() const {
        return hasHDR ? currentRelativeDynamicRange : 1.0;
    }
};

KisSmallColorWidget::KisSmallColorWidget(QWidget* parent)
    : QWidget(parent),
      d(new Private)
{
    d->hue = 0.0;
    d->value = 0;
    d->saturation = 0;
    d->updateAllowed = true;
    d->updateTimer.setInterval(1);
    d->updateTimer.setSingleShot(true);
    connect(&(d->updateTimer), SIGNAL(timeout()), this, SLOT(update()));

    d->resizeUpdateCompressor = new KisSignalCompressor(200, KisSignalCompressor::FIRST_ACTIVE, this);
    connect(d->resizeUpdateCompressor, SIGNAL(timeout()), SLOT(slotUpdatePalettes()));

    d->valueSliderUpdateCompressor = new KisSignalCompressor(100, KisSignalCompressor::FIRST_ACTIVE, this);
    connect(d->valueSliderUpdateCompressor, SIGNAL(timeout()), SLOT(updateSVPalette()));

    const QSurfaceFormat::ColorSpace colorSpace = QSurfaceFormat::DefaultColorSpace;

    d->hueWidget = new KisClickableGLImageWidget(colorSpace, this);
    d->hueWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    d->hueWidget->setHandlePaintingStrategy(new KisClickableGLImageWidget::VerticalLineHandleStrategy);
    connect(d->hueWidget, SIGNAL(selected(const QPointF&)), SLOT(slotHueSliderChanged(const QPointF&)));

    d->valueWidget = new KisClickableGLImageWidget(colorSpace, this);
    d->valueWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    d->valueWidget->setHandlePaintingStrategy(new KisClickableGLImageWidget::CircularHandleStrategy);
    connect(d->valueWidget, SIGNAL(selected(const QPointF&)), SLOT(slotValueSliderChanged(const QPointF&)));

    d->hasHardwareHDR = KisOpenGLModeProber::instance()->useHDRMode();

    if (d->hasHardwareHDR) {
        d->dynamicRange = new KisSliderSpinBox(this);
        d->dynamicRange->setRange(80, 10000);
        d->dynamicRange->setExponentRatio(3.0);
        d->dynamicRange->setSingleStep(1);
        d->dynamicRange->setPageStep(100);
        d->dynamicRange->setSuffix("cd/m²");
        d->dynamicRange->setValue(80.0 * d->currentRelativeDynamicRange);
        connect(d->dynamicRange, SIGNAL(valueChanged(int)), SLOT(slotUpdateDynamicRange(int)));
    }

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(d->hueWidget, 0);
    layout->addWidget(d->valueWidget, 1);

    if (d->dynamicRange) {
        layout->addSpacing(16);
        layout->addWidget(d->dynamicRange, 0);
    }
    setLayout(layout);

    slotUpdatePalettes();
}

KisSmallColorWidget::~KisSmallColorWidget()
{
    delete d;
}

void KisSmallColorWidget::setHue(qreal h)
{
    h = qBound(0.0, h, 1.0);
    d->hue = h;
    tellColorChanged();
    d->valueSliderUpdateCompressor->start();
    d->updateTimer.start();
}

void KisSmallColorWidget::setHSV(qreal h, qreal s, qreal v, bool notifyChanged)
{
    h = qBound(0.0, h, 1.0);
    s = qBound(0.0, s, 1.0);
    v = qBound(0.0, v, 1.0);
    bool newH = !qFuzzyCompare(d->hue, h);
    d->hue = h;
    d->value = v;
    d->saturation = s;
    // TODO: remove and make acyclic!
    if (notifyChanged) {
        tellColorChanged();
    }
    if(newH) {
        d->valueSliderUpdateCompressor->start();
    }
    d->updateTimer.start();
}

void KisSmallColorWidget::setColor(const KoColor &color)
{
    if (!d->updateAllowed) return;

    KIS_SAFE_ASSERT_RECOVER(!d->dynamicRange || d->hasHDR == d->dynamicRange->isEnabled()) {
        slotDisplayConfigurationChanged();
    }

    KIS_SAFE_ASSERT_RECOVER_RETURN(!d->hasHDR || d->hasHardwareHDR);

    const KoColorSpace *cs = d->displayColorConverter->paintingColorSpace();
    if (cs->colorModelId() != RGBAColorModelID) {
        cs = KoColorSpaceRegistry::instance()->rgb8();
    }

    KoColor newColor(color);
    newColor.convertTo(cs);

    QVector<float> channels(4);
    cs->normalisedChannelsValue(newColor.data(), channels);

    float r, g, b;

    if (cs->colorDepthId() == Integer8BitsColorDepthID) {
        r = channels[2];
        g = channels[1];
        b = channels[0];
    } else {
        r = channels[0];
        g = channels[1];
        b = channels[2];
    }

    if (d->hasHDR) {
        qreal rangeCoeff = d->effectiveRelativeDynamicRange();

        if (rangeCoeff < r || rangeCoeff < g || rangeCoeff < b) {
            rangeCoeff = std::max({r, g, b}) * 1.10f;

            const int newMaxLuminance = qRound(80.0 * rangeCoeff);
            slotUpdateDynamicRange(newMaxLuminance);
            d->dynamicRange->setValue(newMaxLuminance);
        }

        r /= rangeCoeff;
        g /= rangeCoeff;
        b /= rangeCoeff;
    } else {
        r = qBound(0.0f, r, 1.0f);
        g = qBound(0.0f, g, 1.0f);
        b = qBound(0.0f, b, 1.0f);
    }

    float denormHue, saturation, value;
    RGBToHSV(r, g, b, &denormHue, &saturation, &value);

    d->hueWidget->setNormalizedPos(QPointF(denormHue / 360.0, 0.0));
    d->valueWidget->setNormalizedPos(QPointF(saturation, 1.0 - value));

    setHSV(denormHue / 360.0, saturation, value, false);
}

void KisSmallColorWidget::slotUpdatePalettes()
{
    updateHuePalette();
    updateSVPalette();
}

namespace {
struct FillHPolicy {
    static inline void getRGB(qreal hue, float xPortionCoeff, float yPortionCoeff,
                              int x, int y, float *r, float *g, float *b) {

        HSVToRGB(xPortionCoeff * x * 360.0f, 1.0, 1.0, r, g, b);
    }
};

struct FillSVPolicy {
    static inline void getRGB(qreal hue, float xPortionCoeff, float yPortionCoeff,
                              int x, int y, float *r, float *g, float *b) {

        HSVToRGB(hue * 360.0, xPortionCoeff * x, 1.0 - yPortionCoeff * y, r, g, b);
    }
};
}

template<class FillPolicy>
void KisSmallColorWidget::uploadPaletteData(KisGLImageWidget *widget, const QSize &size)
{
    KisGLImageF16 image(size);
    const float xPortionCoeff = 1.0 / image.width();
    const float yPortionCoeff = 1.0 / image.height();
    const float rangeCoeff = d->effectiveRelativeDynamicRange();

    const KoColorSpace *generationColorSpace = d->displayColorConverter ? d->displayColorConverter->paintingColorSpace() : 0;
    const KoColorSpace *outputColorSpace = KoColorSpaceRegistry::instance()->
            colorSpace(RGBAColorModelID.id(), Float32BitsColorDepthID.id(),
                       KoColorSpaceRegistry::instance()->rgb8()->profile());

    QSurfaceFormat::ColorSpace surfaceColorSpace = KisOpenGLModeProber::instance()->surfaceformatInUse().colorSpace();

    if (surfaceColorSpace == QSurfaceFormat::sRGBColorSpace) {
        // use the default one!
    } else if (surfaceColorSpace == QSurfaceFormat::scRGBColorSpace) {
        // p709-g10
        outputColorSpace = KoColorSpaceRegistry::instance()->
                colorSpace(RGBAColorModelID.id(), Float32BitsColorDepthID.id());
    } else if (surfaceColorSpace == QSurfaceFormat::bt2020PQColorSpace) {
        qWarning() << "Small Color Selector: output for p2020-pq is not fully supported";
    }

    if (!generationColorSpace || generationColorSpace->colorModelId() != RGBAColorModelID) {
        generationColorSpace = outputColorSpace;
    } else if (generationColorSpace->colorDepthId() != Float32BitsColorDepthID) {
        generationColorSpace = KoColorSpaceRegistry::instance()->
            colorSpace(RGBAColorModelID.id(), Float32BitsColorDepthID.id(), generationColorSpace->profile());
    }

    if (!d->displayColorConverter || d->displayColorConverter->canSkipDisplayConversion(generationColorSpace)) {
        half *pixelPtr = image.data();

        for (int y = 0; y < image.height(); y++) {
            for (int x = 0; x < image.width(); x++) {
                Imf::Rgba &pxl = reinterpret_cast<Imf::Rgba &>(*pixelPtr);

                float r, g, b;
                FillPolicy::getRGB(d->hue, xPortionCoeff, yPortionCoeff, x, y,
                                   &r, &g, &b);

                pxl.r = r * rangeCoeff;
                pxl.g = g * rangeCoeff;
                pxl.b = b * rangeCoeff;
                pxl.a = 1.0;

                pixelPtr += 4;
            }
        }

    } else {
        KIS_SAFE_ASSERT_RECOVER_RETURN(d->displayColorConverter);

        KisFixedPaintDeviceSP device = new KisFixedPaintDevice(generationColorSpace);
        device->setRect(QRect(QPoint(), image.size()));
        device->reallocateBufferWithoutInitialization();
        float *devicePtr = reinterpret_cast<float*>(device->data());

        for (int y = 0; y < image.height(); y++) {
            for (int x = 0; x < image.width(); x++) {
                FillPolicy::getRGB(d->hue, xPortionCoeff, yPortionCoeff, x, y,
                                   devicePtr, devicePtr + 1, devicePtr + 2);

                devicePtr[0] *= rangeCoeff;
                devicePtr[1] *= rangeCoeff;
                devicePtr[2] *= rangeCoeff;
                devicePtr[3] = 1.0;

                devicePtr += 4;
            }
        }

        d->displayColorConverter->applyDisplayFilteringF32(device);

        /**
         * TODO: theoretically, we could skip this step, because the surface format
         * should coincide with the one of the image
         */
        device->convertTo(outputColorSpace);

        half *imagePtr = image.data();
        devicePtr = reinterpret_cast<float*>(device->data());

        for (int y = 0; y < image.height(); y++) {
            for (int x = 0; x < image.width(); x++) {
                imagePtr[0] = devicePtr[0];
                imagePtr[1] = devicePtr[1];
                imagePtr[2] = devicePtr[2];
                imagePtr[3] = devicePtr[3];

                devicePtr += 4;
                imagePtr += 4;
            }
        }
    }

    widget->loadImage(image);
}

void KisSmallColorWidget::updateHuePalette()
{
    uploadPaletteData<FillHPolicy>(d->hueWidget, QSize(width(), d->huePreferredHeight));
}

void KisSmallColorWidget::updateSVPalette()
{
    // TODO: make preferred size be different from image size,
    //       so that we could create the image of the exact size we need
    //KisGLImageF16 image(d->valueWidget->size());
    uploadPaletteData<FillSVPolicy>(d->valueWidget, size());
}

void KisSmallColorWidget::slotHueSliderChanged(const QPointF &pos)
{
    const qreal newHue = pos.x();

    if (!qFuzzyCompare(newHue, d->hue)) {
        setHue(newHue);
    }
}

void KisSmallColorWidget::slotValueSliderChanged(const QPointF &pos)
{
    const qreal newSaturation = pos.x();
    const qreal newValue = 1.0 - pos.y();

    if (!qFuzzyCompare(newSaturation, d->saturation) ||
        !qFuzzyCompare(newValue, d->value)) {

        setHSV(d->hue, newSaturation, newValue);
    }
}

void KisSmallColorWidget::slotUpdateDynamicRange(int maxLuminance)
{
    const qreal oldRange = d->currentRelativeDynamicRange;
    const qreal newRange = qreal(maxLuminance) / 80.0;

    if (qFuzzyCompare(oldRange, newRange)) return;

    float r, g, b;
    float denormHue = d->hue * 360.0;
    float saturation = d->saturation;
    float value = d->value;

    HSVToRGB(denormHue, saturation, value, &r, &g, &b);

    const qreal transformCoeff = newRange / oldRange;

    r = qBound(0.0, r * transformCoeff, 1.0);
    g = qBound(0.0, g * transformCoeff, 1.0);
    b = qBound(0.0, b * transformCoeff, 1.0);

    RGBToHSV(r, g, b, &denormHue, &saturation, &value);

    d->currentRelativeDynamicRange = newRange;
    slotUpdatePalettes();
    setHSV(denormHue / 360.0, saturation, value, false);
    d->hueWidget->setNormalizedPos(QPointF(denormHue / 360.0, 0));
    d->valueWidget->setNormalizedPos(QPointF(saturation, 1.0 - value));
}

void KisSmallColorWidget::setDisplayColorConverter(KisDisplayColorConverter *converter)
{
    d->colorConverterConnections.clear();

    d->displayColorConverter = converter;

    if (d->displayColorConverter) {
        d->colorConverterConnections.addConnection(
            d->displayColorConverter, SIGNAL(displayConfigurationChanged()),
            this, SLOT(slotDisplayConfigurationChanged()));
    }

    slotDisplayConfigurationChanged();
}

void KisSmallColorWidget::slotDisplayConfigurationChanged()
{
    d->hasHDR = false;

    if (d->hasHardwareHDR && d->displayColorConverter) {
        const KoColorSpace *cs = d->displayColorConverter->paintingColorSpace();

        d->hasHDR = cs->colorModelId() == RGBAColorModelID &&
                (cs->colorDepthId() == Float16BitsColorDepthID ||
                 cs->colorDepthId() == Float32BitsColorDepthID ||
                 cs->colorDepthId() == Float64BitsColorDepthID);
    }

    if (d->dynamicRange) {
        d->dynamicRange->setEnabled(d->hasHDR);
    }
    d->hueWidget->setUseHandleOpacity(!d->hasHDR);
    d->valueWidget->setUseHandleOpacity(!d->hasHDR);

    slotUpdatePalettes();
    // TODO: also set the currently selected color again
}

void KisSmallColorWidget::tellColorChanged()
{
    d->updateAllowed = false;

    float r, g, b;
    HSVToRGB(d->hue * 360.0, d->saturation, d->value, &r, &g, &b);

    if (d->hasHDR) {
        const float rangeCoeff = d->effectiveRelativeDynamicRange();

        r *= rangeCoeff;
        g *= rangeCoeff;
        b *= rangeCoeff;
    }

    const KoColorSpace *cs = d->displayColorConverter->paintingColorSpace();
    if (cs->colorModelId() != RGBAColorModelID) {
        cs = KoColorSpaceRegistry::instance()->rgb8();
    }

    QVector<float> values(4);

    if (cs->colorDepthId() == Integer8BitsColorDepthID) {
        values[0] = b;
        values[1] = g;
        values[2] = r;
        values[3] = 1.0f;
    } else {
        values[0] = r;
        values[1] = g;
        values[2] = b;
        values[3] = 1.0f;
    }

    KoColor c(cs);
    cs->fromNormalisedChannelsValue(c.data(), values);
    emit colorChanged(c);

    d->updateAllowed = true;
}

void KisSmallColorWidget::resizeEvent(QResizeEvent * event)
{
    QWidget::resizeEvent(event);
    update();
    d->resizeUpdateCompressor->start();
}

