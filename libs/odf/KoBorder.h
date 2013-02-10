/* This file is part of the KDE project
 *
 * Copyright (C) 2009 Inge wallin <inge@lysator.liu.se>
 * Copyright (C) 2009 Thomas Zander <zander@kde.org>
 * Copyright (C) 2011 Pierre Ducroquet <pinaraf@pinaraf.info>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */


#ifndef KOBORDER_H
#define KOBORDER_H

#include "koodf_export.h"

#include <QColor>
#include <QPen>
#include <QSharedData>
#include <QMetaType>

#include "KoXmlReaderForward.h"
#include "KoGenStyle.h"

class KoGenStyle;
class KoBorderPrivate;


/**
 * A container for all properties of a generic border as defined by ODF.
 *
 * A border is used in at least the following contexts:
 *  - paragraph
 *  - page
 *  - table
 *  - table cell
 *
 */

class KOODF_EXPORT KoBorder
{
public:

    /// The type of border.  Note that some of the border types are legacies from the old Words format.
    enum BorderStyle {
        BorderNone, ///< no border. This value forces the computed value of 'border-width' to be '0'.
        BorderDotted,   ///< The border is a series of dots.
        BorderDashed,   ///< The border is a series of short line segments.
        BorderSolid,    ///< The border is a single line segment.
        BorderDouble,   ///< The border is two solid lines. The sum of the two lines and the space between them equals the value of 'border-width'.
        BorderGroove,   ///< The border looks as though it were carved into the canvas. (old words type)
        BorderRidge,    ///< The opposite of 'groove': the border looks as though it were coming out of the canvas. (old words type)
        BorderInset,    ///< The border makes the entire box look as though it were embedded in the canvas. (old words type)
        BorderOutset,   ///< The opposite of 'inset': the border makes the entire box look as though it were coming out of the canvas. (old words type)

        BorderDashedLong,    ///< Dashed single border with long spaces
        BorderTriple,    ///< Triple lined border
        BorderSlash,    ///< slash border
        BorderWave,    ///< wave border
        BorderDoubleWave,    ///< double wave border

        // words legacy
        BorderDashDot,
        BorderDashDotDot
    };

    // The "rect" we refer to below is the rectangle around the object
    // with the border. This could be a page, a cell, a paragraph, etc.
    //
    // FIXME: Should be called Edge instead?  It's used in some places.
    enum Side {
        Top = 0, ///< References the border at the top of the rect
        Left,    ///< References the border at the left side of the rect
        Bottom,  ///< References the border at the bottom of the rect
        Right,   ///< References the border at the right side of the rect
        TopLeftToBottomRight, ///< References the border from top, left corner to bottom, right corner of cell
        BottomLeftToTopRight  ///< References the border from bottom, left corner to top, right corner of cell
    };

    /// Holds data about one border line.
    struct KOODF_EXPORT BorderData {
        BorderData();
        BorderStyle  style; ///< The border style. (see KoBorder::BorderStyle)
        qreal spacing;

        QPen innerPen;
        QPen outerPen;

        /// Compare the border data with another one
        bool operator==(const BorderData &other) const;
    };


    /// Constructor
    KoBorder();
    KoBorder(const KoBorder &kb);

    /// Destructor
    ~KoBorder();

    /// Assignment
    KoBorder &operator=(const KoBorder &other);
    /// Compare the border with the other one
    bool operator==(const KoBorder &other) const;
    bool operator!=(const KoBorder &other) const { return !operator==(other); }

    void setBorderStyle(Side side, BorderStyle style);
    BorderStyle borderStyle(Side side) const;
    void setBorderColor(Side side, const QColor &color);
    QColor borderColor(Side side) const;
    void setBorderWidth(Side side, qreal width);
    qreal borderWidth(Side side) const;
    void setInnerBorderWidth(Side side, qreal width);
    qreal innerBorderWidth(Side side) const;
    void setBorderSpacing(Side side, qreal width);
    qreal borderSpacing(Side side) const;

    BorderData borderData(Side side) const;
    void setBorderData(Side side, const BorderData &data);

    bool hasBorder() const;
    bool hasBorder(Side side) const;

    /**
     * Load the style from the element
     *
     * @param style  the element containing the style to read from
     * @return true when border attributes were found
     */
    bool loadOdf(const KoXmlElement &style);
    void saveOdf(KoGenStyle &style, KoGenStyle::PropertyType type = KoGenStyle::DefaultType) const;


    // Some public functions used in other places where borders are handled.
    // Example: KoParagraphStyle
    // FIXME: These places should be made to use KoBorder instead.
    static BorderStyle odfBorderStyle(const QString &borderstyle, bool *converted = 0);
    static QString odfBorderStyleString(BorderStyle borderstyle);
    static QString msoBorderStyleString(BorderStyle borderstyle);

private:
    QSharedDataPointer<KoBorderPrivate> d;
};

Q_DECLARE_METATYPE(KoBorder)


#endif
