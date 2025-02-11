/*
 * Copyright (C) 2018 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "RenderTreeBuilderMathML.h"

#include "RenderMathMLFenced.h"
#include "RenderMathMLFencedOperator.h"

namespace WebCore {

RenderTreeBuilder::MathML::MathML(RenderTreeBuilder& builder)
    : m_builder(builder)
{
}

RenderPtr<RenderMathMLFencedOperator> RenderTreeBuilder::MathML::createMathMLOperator(RenderMathMLFenced& parent, const String& operatorString,
    MathMLOperatorDictionary::Form form, MathMLOperatorDictionary::Flag flag)
{
    RenderPtr<RenderMathMLFencedOperator> newOperator = createRenderer<RenderMathMLFencedOperator>(parent.document(), RenderStyle::createAnonymousStyleWithDisplay(parent.style(), BLOCK), operatorString, form, flag);
    newOperator->initializeStyle();
    return newOperator;
}

void RenderTreeBuilder::MathML::makeFences(RenderMathMLFenced& parent)
{
    auto openFence = createMathMLOperator(parent, parent.openingBrace(), MathMLOperatorDictionary::Prefix, MathMLOperatorDictionary::Fence);
    parent.RenderMathMLRow::addChild(m_builder, WTFMove(openFence), parent.firstChild());

    auto closeFence = createMathMLOperator(parent, parent.closingBrace(), MathMLOperatorDictionary::Postfix, MathMLOperatorDictionary::Fence);
    parent.setCloseFenceRenderer(*closeFence);
    parent.RenderMathMLRow::addChild(m_builder, WTFMove(closeFence));
}

void RenderTreeBuilder::MathML::insertChild(RenderMathMLFenced& parent, RenderPtr<RenderObject> child, RenderObject* beforeChild)
{
    if (!parent.firstChild())
        makeFences(parent);
    // FIXME: Adding or removing a child should possibly cause all later separators to shift places
    // if they're different, as later child positions change by +1 or -1.
    // This should also handle surrogate pairs. See https://bugs.webkit.org/show_bug.cgi?id=125938.
    RenderPtr<RenderMathMLFencedOperator> separatorRenderer;
    auto* separators = parent.separators();
    if (separators) {
        unsigned count = 0;
        for (Node* position = child->node(); position; position = position->previousSibling()) {
            if (position->isElementNode())
                count++;
        }
        if (!beforeChild) {
            // We're adding at the end (before the closing fence), so a new separator would go before the new child, not after it.
            --count;
        }
        // |count| is now the number of element children that will be before our new separator, i.e. it's the 1-based index of the separator.

        if (count > 0) {
            UChar separator;

            // Use the last separator if we've run out of specified separators.
            if (count > separators->length())
                separator = (*separators)[separators->length() - 1];
            else
                separator = (*separators)[count - 1];

            StringBuilder stringBuilder;
            stringBuilder.append(separator);
            separatorRenderer = createMathMLOperator(parent, stringBuilder.toString(), MathMLOperatorDictionary::Infix, MathMLOperatorDictionary::Separator);
        }
    }

    if (beforeChild) {
        // Adding |x| before an existing |y| e.g. in element (y) - first insert our new child |x|, then its separator, to get (x, y).
        parent.RenderMathMLRow::addChild(m_builder, WTFMove(child), beforeChild);
        if (separatorRenderer)
            parent.RenderMathMLRow::addChild(m_builder, WTFMove(separatorRenderer), beforeChild);
    } else {
        // Adding |y| at the end of an existing element e.g. (x) - insert the separator first before the closing fence, then |y|, to get (x, y).
        if (separatorRenderer)
            parent.RenderMathMLRow::addChild(m_builder, WTFMove(separatorRenderer), parent.closeFenceRenderer());
        parent.RenderMathMLRow::addChild(m_builder, WTFMove(child), parent.closeFenceRenderer());
    }
}

}
