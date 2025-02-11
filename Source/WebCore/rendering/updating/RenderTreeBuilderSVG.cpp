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
#include "RenderTreeBuilderSVG.h"

#include "RenderSVGContainer.h"
#include "RenderSVGInline.h"
#include "RenderSVGRoot.h"
#include "RenderSVGText.h"
#include "SVGResourcesCache.h"

namespace WebCore {

RenderTreeBuilder::SVG::SVG(RenderTreeBuilder& builder)
    : m_builder(builder)
{
}

void RenderTreeBuilder::SVG::insertChild(RenderSVGContainer& parent, RenderPtr<RenderObject> child, RenderObject* beforeChild)
{
    auto& childToAdd = *child;
    parent.RenderElement::addChild(m_builder, WTFMove(child), beforeChild);
    SVGResourcesCache::clientWasAddedToTree(childToAdd);
}

void RenderTreeBuilder::SVG::insertChild(RenderSVGInline& parent, RenderPtr<RenderObject> child, RenderObject* beforeChild)
{
    auto& childToAdd = *child;
    m_builder.insertChildToRenderInline(parent, WTFMove(child), beforeChild);
    SVGResourcesCache::clientWasAddedToTree(childToAdd);

    if (auto* textAncestor = RenderSVGText::locateRenderSVGTextAncestor(parent))
        textAncestor->subtreeChildWasAdded(&childToAdd);
}

void RenderTreeBuilder::SVG::insertChild(RenderSVGRoot& parent, RenderPtr<RenderObject> child, RenderObject* beforeChild)
{
    auto& childToAdd = *child;
    parent.RenderReplaced::addChild(m_builder, WTFMove(child), beforeChild);
    SVGResourcesCache::clientWasAddedToTree(childToAdd);
}

void RenderTreeBuilder::SVG::insertChild(RenderSVGText& parent, RenderPtr<RenderObject> child, RenderObject* beforeChild)
{
    auto& childToAdd = *child;
    m_builder.insertChildToRenderBlockFlow(parent, WTFMove(child), beforeChild);

    SVGResourcesCache::clientWasAddedToTree(childToAdd);
    parent.subtreeChildWasAdded(&childToAdd);
}

}
