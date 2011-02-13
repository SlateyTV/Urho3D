//
// Urho3D Engine
// Copyright (c) 2008-2011 Lasse ��rni
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#include "Precompiled.h"
#include "Font.h"
#include "Log.h"
#include "Profiler.h"
#include "ResourceCache.h"
#include "StringUtils.h"
#include "Text.h"
#include "Texture2D.h"

#include "DebugNew.h"

Text::Text(const std::string& name, const std::string& text) :
    UIElement(name),
    mFontSize(DEFAULT_FONT_SIZE),
    mText(text),
    mTextAlignment(HA_LEFT),
    mRowSpacing(1.0f),
    mWordwrap(false),
    mSelectionStart(0),
    mSelectionLength(0),
    mSelectionColor(Color(0.0f, 0.0f, 0.0f, 0.0f)),
    mHoverColor(Color(0.0f, 0.0f, 0.0f, 0.0f)),
    mRowHeight(0)
{
}

Text::~Text()
{
}

void Text::setStyle(const XMLElement& element, ResourceCache* cache)
{
    UIElement::setStyle(element, cache);
    
    // Set word wrap first to preserve any pre-defined width
    if (element.hasChildElement("wordwrap"))
        setWordwrap(element.getChildElement("wordwrap").getBool("enable"));
    if (element.hasChildElement("font"))
    {
        XMLElement fontElem = element.getChildElement("font");
        setFont(cache->getResource<Font>(fontElem.getString("name")), fontElem.getInt("size"));
    }
    if (element.hasChildElement("textalignment"))
    {
        std::string horiz = element.getChildElement("textalignment").getStringLower("value");
        if (horiz == "left")
            setTextAlignment(HA_LEFT);
        if (horiz == "center")
            setTextAlignment(HA_CENTER);
        if (horiz == "right")
            setTextAlignment(HA_RIGHT);
    }
    if (element.hasChildElement("rowspacing"))
        setRowSpacing(element.getChildElement("rowspacing").getFloat("value"));
    if (element.hasChildElement("selection"))
    {
        XMLElement selectionElem = element.getChildElement("selection");
        setSelection(selectionElem.getInt("start"), selectionElem.getInt("length"));
    }
    if (element.hasChildElement("selectioncolor"))
        setSelectionColor(element.getChildElement("selectioncolor").getColor("value"));
    if (element.hasChildElement("hovercolor"))
        setHoverColor(element.getChildElement("hovercolor").getColor("value"));
    if (element.hasChildElement("text"))
    {
        std::string text = element.getChildElement("text").getString("value");
        replaceInPlace(text, "\\n", "\n");
        setText(text);
    }
}

void Text::getBatches(std::vector<UIBatch>& batches, std::vector<UIQuad>& quads, const IntRect& currentScissor)
{
    // Hovering batch
    if (((mHovering) || (mSelected)) && (mHoverColor.mA > 0.0f))
    {
        UIBatch batch;
        batch.begin(&quads);
        batch.mBlendMode = BLEND_ALPHA;
        batch.mScissor = currentScissor;
        batch.mTexture = 0;
        batch.addQuad(*this, 0, 0, getWidth(), getHeight(), 0, 0, 0, 0, mHoverColor);
        UIBatch::addOrMerge(batch, batches);
    }
    
    // Selection batch
    if ((mSelectionLength) && (mCharSizes.size() >= mSelectionStart + mSelectionLength) && (mSelectionColor.mA > 0.0f))
    {
        UIBatch batch;
        batch.begin(&quads);
        batch.mBlendMode = BLEND_ALPHA;
        batch.mScissor = currentScissor;
        batch.mTexture = 0;
        
        IntVector2 currentStart = mCharPositions[mSelectionStart];
        IntVector2 currentEnd = currentStart;
        for (unsigned i = mSelectionStart; i < mSelectionStart + mSelectionLength; ++i)
        {
            // Check if row changes, and start a new quad in that case
            if ((mCharSizes[i].mX) && (mCharSizes[i].mY))
            {
                if (mCharPositions[i].mY != currentStart.mY)
                {
                    batch.addQuad(*this, currentStart.mX, currentStart.mY, currentEnd.mX - currentStart.mX, currentEnd.mY - currentStart.mY,
                        0, 0, 0, 0, mSelectionColor);
                    currentStart = mCharPositions[i];
                    currentEnd = currentStart + mCharSizes[i];
                }
                else
                {
                    currentEnd.mX += mCharSizes[i].mX;
                    currentEnd.mY = max(currentStart.mY + mCharSizes[i].mY, currentEnd.mY);
                }
            }
        }
        if (currentEnd != currentStart)
        {
            batch.addQuad(*this, currentStart.mX, currentStart.mY, currentEnd.mX - currentStart.mX, currentEnd.mY - currentStart.mY,
                0, 0, 0, 0, mSelectionColor);
        }
        
        UIBatch::addOrMerge(batch, batches);
    }
    
    // Text batch
    if (mFont)
    {
        const FontFace* face = mFont->getFace(mFontSize);
        
        UIBatch batch;
        batch.begin(&quads);
        batch.mBlendMode = BLEND_ALPHA;
        batch.mScissor = currentScissor;
        batch.mTexture = face->mTexture;
        
        unsigned rowIndex = 0;
        int x = getRowStartPosition(rowIndex);
        int y = 0;
        
        for (unsigned i = 0; i < mPrintText.length(); ++i)
        {
            unsigned char c = (unsigned char)mPrintText[i];
            
            if (c != '\n')
            {
                const FontGlyph& glyph = face->mGlyphs[face->mGlyphIndex[c]];
                if (c != ' ')
                    batch.addQuad(*this, x + glyph.mOffsetX, y + glyph.mOffsetY, glyph.mWidth, glyph.mHeight, glyph.mX, glyph.mY);
                x += glyph.mAdvanceX;
            }
            else
            {
                rowIndex++;
                x = getRowStartPosition(rowIndex);
                y += mRowHeight;
            }
        }
        
        UIBatch::addOrMerge(batch, batches);
    }
    
    // Reset hovering for next frame
    mHovering = false;
}

void Text::onResize()
{
    if (mWordwrap)
        updateText();
}

bool Text::setFont(Font* font, int size)
{
    if (!font)
    {
        LOGERROR("Null font for Text");
        return false;
    }
    
    if ((font != mFont) || (size != mFontSize))
    {
        mFont = font;
        mFontSize = max(size, 1);
        updateText();
    }
    
    return true;
}

void Text::setText(const std::string& text)
{
    mText = text;
    
    validateSelection();
    updateText();
}

void Text::setTextAlignment(HorizontalAlignment align)
{
    if (align != mTextAlignment)
    {
        mTextAlignment = align;
        updateText();
    }
}

void Text::setRowSpacing(float spacing)
{
    if (spacing != mRowSpacing)
    {
        mRowSpacing = max(spacing, 0.5f);
        updateText();
    }
}

void Text::setWordwrap(bool enable)
{
    if (enable != mWordwrap)
    {
        mWordwrap = enable;
        updateText();
    }
}

void Text::setSelection(unsigned start, unsigned length)
{
    mSelectionStart = start;
    mSelectionLength = length;
    validateSelection();
}

void Text::clearSelection()
{
    mSelectionStart = 0;
    mSelectionLength = 0;
}

void Text::setSelectionColor(const Color& color)
{
    mSelectionColor = color;
}

void Text::setHoverColor(const Color& color)
{
    mHoverColor = color;
}

void Text::updateText(bool inResize)
{
    int width = 0;
    int height = 0;
    
    mRowWidths.clear();
    mPrintText.clear();
    
    static std::vector<unsigned> printToText;
    printToText.clear();
    
    if (mFont)
    {
        const FontFace* face = mFont->getFace(mFontSize);
        mRowHeight = face->mRowHeight;
        int rowWidth = 0;
        int rowHeight = (int)(mRowSpacing * mRowHeight);
        
        // First see if the text must be split up
        if (!mWordwrap)
        {
            mPrintText = mText;
            printToText.resize(mText.length());
            for (unsigned i = 0; i < mText.length(); ++i)
                printToText[i] = i;
        }
        else
        {
            int maxWidth = getWidth();
            unsigned nextBreak = 0;
            unsigned lineStart = 0;
            for (unsigned i = 0; i < mText.length(); ++i)
            {
                unsigned j;
                if (mText[i] != '\n')
                {
                    bool ok = true;
                    
                    if (nextBreak <= i)
                    {
                        int futureRowWidth = rowWidth;
                        for (j = i; j < mText.length(); ++j)
                        {
                            if ((mText[j] == ' ') || (mText[j] == '\n'))
                            {
                                nextBreak = j;
                                break;
                            }
                            futureRowWidth += face->mGlyphs[face->mGlyphIndex[mText[j]]].mAdvanceX;
                            if ((mText[j] == '-') && (futureRowWidth <= maxWidth))
                            {
                                nextBreak = j + 1;
                                break;
                            }
                            if (futureRowWidth > maxWidth)
                            {
                                ok = false;
                                break;
                            }
                        }
                    }
                    
                    if (!ok)
                    {
                        // If did not find any breaks on the line, copy until j, or at least 1 char, to prevent infinite loop
                        if (nextBreak == lineStart)
                        {
                            int copyLength = max(j - i, 1);
                            mPrintText.append(mText.substr(i, copyLength));
                            for (int k = 0; k < copyLength; ++k)
                            {
                                printToText.push_back(i);
                                ++i;
                            }
                        }
                        mPrintText += '\n';
                        printToText.push_back(min((int)i, (int)mText.length() - 1));
                        rowWidth = 0;
                        nextBreak = lineStart = i;
                    }
                    
                    if (i < mText.length())
                    {
                        // When copying a space, we may be over row width
                        rowWidth += face->mGlyphs[face->mGlyphIndex[mText[i]]].mAdvanceX;
                        if (rowWidth <= maxWidth)
                        {
                            mPrintText += mText[i];
                            printToText.push_back(i);
                        }
                    }
                }
                else
                {
                    mPrintText += '\n';
                    printToText.push_back(min((int)i, (int)mText.length() - 1));
                    rowWidth = 0;
                    nextBreak = lineStart = i;
                }
            }
        }
        
        rowWidth = 0;
        
        for (unsigned i = 0; i < mPrintText.length(); ++i)
        {
            unsigned char c = (unsigned char)mPrintText[i];
            
            if (c != '\n')
            {
                const FontGlyph& glyph = face->mGlyphs[face->mGlyphIndex[c]];
                rowWidth += glyph.mAdvanceX;
            }
            else
            {
                width = max(width, rowWidth);
                height += rowHeight;
                mRowWidths.push_back(rowWidth);
                rowWidth = 0;
            }
        }
        
        if (rowWidth)
        {
            width = max(width, rowWidth);
            height += rowHeight;
            mRowWidths.push_back(rowWidth);
        }
        
        // Set row height even if text is empty
        if (!height)
            height = rowHeight;
        
        // Store position & size of each character
        mCharPositions.resize(mText.length() + 1);
        mCharSizes.resize(mText.length());
        
        unsigned rowIndex = 0;
        int x = getRowStartPosition(rowIndex);
        int y = 0;
        for (unsigned i = 0; i < mPrintText.length(); ++i)
        {
            mCharPositions[printToText[i]] = IntVector2(x, y);
            unsigned char c = (unsigned char)mPrintText[i];
            if (c != '\n')
            {
                const FontGlyph& glyph = face->mGlyphs[face->mGlyphIndex[c]];
                mCharSizes[printToText[i]] = IntVector2(glyph.mAdvanceX, mRowHeight);
                x += glyph.mAdvanceX;
            }
            else
            {
                mCharSizes[printToText[i]] = IntVector2::sZero;
                rowIndex++;
                x = getRowStartPosition(rowIndex);
                y += rowHeight;
            }
        }
        // Store the ending position
        mCharPositions[mText.length()] = IntVector2(x, y);
    }
    
    if (mWordwrap)
        setHeight(height);
    else
        setSize(width, height);
}

void Text::validateSelection()
{
    unsigned textLength = mText.length();
    
    if (textLength)
    {
        if (mSelectionStart >= textLength)
            mSelectionStart = textLength - 1;
        if (mSelectionStart + mSelectionLength > textLength)
            mSelectionLength = textLength - mSelectionStart;
    }
    else
    {
        mSelectionStart = 0;
        mSelectionLength = 0;
    }
}

int Text::getRowStartPosition(unsigned rowIndex) const
{
    int rowWidth = 0;
    
    if (rowIndex < mRowWidths.size())
        rowWidth = mRowWidths[rowIndex];
    
    switch (mTextAlignment)
    {
    default:
        return 0;
    case HA_CENTER:
        return (getSize().mX - rowWidth) / 2;
    case HA_RIGHT:
        return getSize().mX - rowWidth;
    }
}
