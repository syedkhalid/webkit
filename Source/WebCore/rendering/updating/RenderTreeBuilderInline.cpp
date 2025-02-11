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
#include "RenderTreeBuilderInline.h"

#include "RenderChildIterator.h"
#include "RenderFullScreen.h"
#include "RenderInline.h"
#include "RenderTable.h"

namespace WebCore {

static bool canUseAsParentForContinuation(const RenderObject* renderer)
{
    if (!renderer)
        return false;
    if (!is<RenderBlock>(renderer) && renderer->isAnonymous())
        return false;
    if (is<RenderTable>(renderer))
        return false;
    return true;
}

static RenderBoxModelObject* nextContinuation(RenderObject* renderer)
{
    if (is<RenderInline>(*renderer) && !renderer->isReplaced())
        return downcast<RenderInline>(*renderer).continuation();
    return downcast<RenderBlock>(*renderer).inlineContinuation();
}

static RenderBoxModelObject* continuationBefore(RenderInline& parent, RenderObject* beforeChild)
{
    if (beforeChild && beforeChild->parent() == &parent)
        return &parent;

    RenderBoxModelObject* curr = nextContinuation(&parent);
    RenderBoxModelObject* nextToLast = &parent;
    RenderBoxModelObject* last = &parent;
    while (curr) {
        if (beforeChild && beforeChild->parent() == curr) {
            if (curr->firstChild() == beforeChild)
                return last;
            return curr;
        }

        nextToLast = last;
        last = curr;
        curr = nextContinuation(curr);
    }

    if (!beforeChild && !last->firstChild())
        return nextToLast;
    return last;
}

static RenderPtr<RenderInline> cloneAsContinuation(RenderInline& renderer)
{
    RenderPtr<RenderInline> cloneInline = createRenderer<RenderInline>(*renderer.element(), RenderStyle::clone(renderer.style()));
    cloneInline->initializeStyle();
    cloneInline->setFragmentedFlowState(renderer.fragmentedFlowState());
    cloneInline->setHasOutlineAutoAncestor(renderer.hasOutlineAutoAncestor());
    cloneInline->setIsContinuation();
    return cloneInline;
}

static RenderElement* inFlowPositionedInlineAncestor(RenderElement& renderer)
{
    auto* ancestor = &renderer;
    while (ancestor && ancestor->isRenderInline()) {
        if (ancestor->isInFlowPositioned())
            return ancestor;
        ancestor = ancestor->parent();
    }
    return nullptr;
}

RenderTreeBuilder::Inline::Inline(RenderTreeBuilder& builder)
    : m_builder(builder)
{
}

void RenderTreeBuilder::Inline::insertChild(RenderInline& parent, RenderPtr<RenderObject> child, RenderObject* beforeChild)
{
    auto* beforeChildOrPlaceholder = beforeChild;
    if (auto* fragmentedFlow = parent.enclosingFragmentedFlow())
        beforeChildOrPlaceholder = fragmentedFlow->resolveMovedChild(beforeChild);
    if (parent.continuation()) {
        insertChildToContinuation(parent, WTFMove(child), beforeChildOrPlaceholder);
        return;
    }
    insertChildIgnoringContinuation(parent, WTFMove(child), beforeChildOrPlaceholder);
}

void RenderTreeBuilder::Inline::insertChildToContinuation(RenderInline& parent, RenderPtr<RenderObject> child, RenderObject* beforeChild)
{
    auto* flow = continuationBefore(parent, beforeChild);
    // It may or may not be the direct parent of the beforeChild.
    RenderBoxModelObject* beforeChildAncestor = nullptr;
    if (!beforeChild) {
        auto* continuation = nextContinuation(flow);
        beforeChildAncestor = continuation ? continuation : flow;
    } else if (canUseAsParentForContinuation(beforeChild->parent()))
        beforeChildAncestor = downcast<RenderBoxModelObject>(beforeChild->parent());
    else if (beforeChild->parent()) {
        // In case of anonymous wrappers, the parent of the beforeChild is mostly irrelevant. What we need is the topmost wrapper.
        auto* parent = beforeChild->parent();
        while (parent && parent->parent() && parent->parent()->isAnonymous()) {
            // The ancestor candidate needs to be inside the continuation.
            if (parent->isContinuation())
                break;
            parent = parent->parent();
        }
        ASSERT(parent && parent->parent());
        beforeChildAncestor = downcast<RenderBoxModelObject>(parent->parent());
    } else
        ASSERT_NOT_REACHED();

    if (child->isFloatingOrOutOfFlowPositioned())
        return beforeChildAncestor->addChildIgnoringContinuation(m_builder, WTFMove(child), beforeChild);

    if (flow == beforeChildAncestor)
        return flow->addChildIgnoringContinuation(m_builder, WTFMove(child), beforeChild);
    // A continuation always consists of two potential candidates: an inline or an anonymous
    // block box holding block children.
    bool childInline = newChildIsInline(parent, *child);
    // The goal here is to match up if we can, so that we can coalesce and create the
    // minimal # of continuations needed for the inline.
    if (childInline == beforeChildAncestor->isInline())
        return beforeChildAncestor->addChildIgnoringContinuation(m_builder, WTFMove(child), beforeChild);
    if (flow->isInline() == childInline)
        return flow->addChildIgnoringContinuation(m_builder, WTFMove(child)); // Just treat like an append.
    return beforeChildAncestor->addChildIgnoringContinuation(m_builder, WTFMove(child), beforeChild);
}

void RenderTreeBuilder::Inline::insertChildIgnoringContinuation(RenderInline& parent, RenderPtr<RenderObject> child, RenderObject* beforeChild)
{
    // Make sure we don't append things after :after-generated content if we have it.
    if (!beforeChild && parent.isAfterContent(parent.lastChild()))
        beforeChild = parent.lastChild();

    bool childInline = newChildIsInline(parent, *child);
    // This code is for the old block-inside-inline model that uses continuations.
    if (!childInline && !child->isFloatingOrOutOfFlowPositioned()) {
        // We are placing a block inside an inline. We have to perform a split of this
        // inline into continuations. This involves creating an anonymous block box to hold
        // |newChild|. We then make that block box a continuation of this inline. We take all of
        // the children after |beforeChild| and put them in a clone of this object.
        auto newStyle = RenderStyle::createAnonymousStyleWithDisplay(parent.style(), BLOCK);

        // If inside an inline affected by in-flow positioning the block needs to be affected by it too.
        // Giving the block a layer like this allows it to collect the x/y offsets from inline parents later.
        if (auto positionedAncestor = inFlowPositionedInlineAncestor(parent))
            newStyle.setPosition(positionedAncestor->style().position());

        auto newBox = createRenderer<RenderBlockFlow>(parent.document(), WTFMove(newStyle));
        newBox->initializeStyle();
        newBox->setIsContinuation();
        RenderBoxModelObject* oldContinuation = parent.continuation();
        if (oldContinuation)
            oldContinuation->removeFromContinuationChain();
        newBox->insertIntoContinuationChainAfter(parent);

        splitFlow(parent, beforeChild, WTFMove(newBox), WTFMove(child), oldContinuation);
        return;
    }

    auto& childToAdd = *child;
    parent.RenderBoxModelObject::addChild(m_builder, WTFMove(child), beforeChild);
    childToAdd.setNeedsLayoutAndPrefWidthsRecalc();
}

void RenderTreeBuilder::Inline::splitFlow(RenderInline& parent, RenderObject* beforeChild, RenderPtr<RenderBlock> newBlockBox, RenderPtr<RenderObject> child, RenderBoxModelObject* oldCont)
{
    auto& addedBlockBox = *newBlockBox;
    RenderBlock* pre = nullptr;
    RenderBlock* block = parent.containingBlock();

    // Delete our line boxes before we do the inline split into continuations.
    block->deleteLines();

    RenderPtr<RenderBlock> createdPre;
    bool madeNewBeforeBlock = false;
    if (block->isAnonymousBlock() && (!block->parent() || !block->parent()->createsAnonymousWrapper())) {
        // We can reuse this block and make it the preBlock of the next continuation.
        pre = block;
        pre->removePositionedObjects(nullptr);
        // FIXME-BLOCKFLOW: The enclosing method should likely be switched over
        // to only work on RenderBlockFlow, in which case this conversion can be
        // removed.
        if (is<RenderBlockFlow>(*pre))
            downcast<RenderBlockFlow>(*pre).removeFloatingObjects();
        block = block->containingBlock();
    } else {
        // No anonymous block available for use. Make one.
        createdPre = block->createAnonymousBlock();
        pre = createdPre.get();
        madeNewBeforeBlock = true;
    }

    auto createdPost = pre->createAnonymousBoxWithSameTypeAs(*block);
    auto& post = downcast<RenderBlock>(*createdPost);

    RenderObject* boxFirst = madeNewBeforeBlock ? block->firstChild() : pre->nextSibling();
    if (createdPre)
        block->insertChildInternal(WTFMove(createdPre), boxFirst);
    block->insertChildInternal(WTFMove(newBlockBox), boxFirst);
    block->insertChildInternal(WTFMove(createdPost), boxFirst);
    block->setChildrenInline(false);

    if (madeNewBeforeBlock) {
        RenderObject* o = boxFirst;
        while (o) {
            RenderObject* no = o;
            o = no->nextSibling();
            auto childToMove = block->takeChildInternal(*no);
            pre->insertChildInternal(WTFMove(childToMove), nullptr);
            no->setNeedsLayoutAndPrefWidthsRecalc();
        }
    }

    splitInlines(parent, pre, &post, &addedBlockBox, beforeChild, oldCont);

    // We already know the newBlockBox isn't going to contain inline kids, so avoid wasting
    // time in makeChildrenNonInline by just setting this explicitly up front.
    addedBlockBox.setChildrenInline(false);

    // We delayed adding the newChild until now so that the |newBlockBox| would be fully
    // connected, thus allowing newChild access to a renderArena should it need
    // to wrap itself in additional boxes (e.g., table construction).
    m_builder.insertChild(addedBlockBox, WTFMove(child));

    // Always just do a full layout in order to ensure that line boxes (especially wrappers for images)
    // get deleted properly. Because objects moves from the pre block into the post block, we want to
    // make new line boxes instead of leaving the old line boxes around.
    pre->setNeedsLayoutAndPrefWidthsRecalc();
    block->setNeedsLayoutAndPrefWidthsRecalc();
    post.setNeedsLayoutAndPrefWidthsRecalc();
}

void RenderTreeBuilder::Inline::splitInlines(RenderInline& parent, RenderBlock* fromBlock, RenderBlock* toBlock, RenderBlock* middleBlock, RenderObject* beforeChild, RenderBoxModelObject* oldCont)
{
    // Create a clone of this inline.
    RenderPtr<RenderInline> cloneInline = cloneAsContinuation(parent);
#if ENABLE(FULLSCREEN_API)
    // If we're splitting the inline containing the fullscreened element,
    // |beforeChild| may be the renderer for the fullscreened element. However,
    // that renderer is wrapped in a RenderFullScreen, so |this| is not its
    // parent. Since the splitting logic expects |this| to be the parent, set
    // |beforeChild| to be the RenderFullScreen.
    const Element* fullScreenElement = parent.document().webkitCurrentFullScreenElement();
    if (fullScreenElement && beforeChild && beforeChild->node() == fullScreenElement)
        beforeChild = parent.document().fullScreenRenderer();
#endif
    // Now take all of the children from beforeChild to the end and remove
    // them from |this| and place them in the clone.
    for (RenderObject* rendererToMove = beforeChild; rendererToMove;) {
        RenderObject* nextSibling = rendererToMove->nextSibling();
        // When anonymous wrapper is present, we might need to move the whole subtree instead.
        if (rendererToMove->parent() != &parent) {
            auto* anonymousParent = rendererToMove->parent();
            while (anonymousParent && anonymousParent->parent() != &parent) {
                ASSERT(anonymousParent->isAnonymous());
                anonymousParent = anonymousParent->parent();
            }
            if (!anonymousParent) {
                ASSERT_NOT_REACHED();
                break;
            }
            // If beforeChild is the first child in the subtree, we could just move the whole subtree.
            if (!rendererToMove->previousSibling()) {
                // Reparent the whole anonymous wrapper tree.
                rendererToMove = anonymousParent;
                // Skip to the next sibling that is not in this subtree.
                nextSibling = anonymousParent->nextSibling();
            } else if (!rendererToMove->nextSibling()) {
                // This is the last renderer in the subtree. We need to jump out of the wrapper subtree, so that
                // the siblings are getting reparented too.
                nextSibling = anonymousParent->nextSibling();
            }
            // Otherwise just move the renderer to the inline clone. Should the renderer need an anon
            // wrapper, the addChild() will generate one for it.
            // FIXME: When the anonymous wrapper has multiple children, we end up traversing up to the topmost wrapper
            // every time, which is a bit wasteful.
        }
        auto childToMove = rendererToMove->parent()->takeChildInternal(*rendererToMove);
        cloneInline->addChildIgnoringContinuation(m_builder, WTFMove(childToMove));
        rendererToMove->setNeedsLayoutAndPrefWidthsRecalc();
        rendererToMove = nextSibling;
    }
    // Hook |clone| up as the continuation of the middle block.
    cloneInline->insertIntoContinuationChainAfter(*middleBlock);
    if (oldCont)
        oldCont->insertIntoContinuationChainAfter(*cloneInline);

    // We have been reparented and are now under the fromBlock. We need
    // to walk up our inline parent chain until we hit the containing block.
    // Once we hit the containing block we're done.
    RenderBoxModelObject* current = downcast<RenderBoxModelObject>(parent.parent());
    RenderBoxModelObject* currentChild = &parent;

    // FIXME: Because splitting is O(n^2) as tags nest pathologically, we cap the depth at which we're willing to clone.
    // There will eventually be a better approach to this problem that will let us nest to a much
    // greater depth (see bugzilla bug 13430) but for now we have a limit. This *will* result in
    // incorrect rendering, but the alternative is to hang forever.
    unsigned splitDepth = 1;
    const unsigned cMaxSplitDepth = 200;
    while (current && current != fromBlock) {
        if (splitDepth < cMaxSplitDepth) {
            // Create a new clone.
            RenderPtr<RenderInline> cloneChild = WTFMove(cloneInline);
            cloneInline = cloneAsContinuation(downcast<RenderInline>(*current));

            // Insert our child clone as the first child.
            cloneInline->addChildIgnoringContinuation(m_builder, WTFMove(cloneChild));

            // Hook the clone up as a continuation of |curr|.
            cloneInline->insertIntoContinuationChainAfter(*current);

            // Now we need to take all of the children starting from the first child
            // *after* currentChild and append them all to the clone.
            for (auto* sibling = currentChild->nextSibling(); sibling;) {
                auto* next = sibling->nextSibling();
                auto childToMove = current->takeChildInternal(*sibling);
                cloneInline->addChildIgnoringContinuation(m_builder, WTFMove(childToMove));
                sibling->setNeedsLayoutAndPrefWidthsRecalc();
                sibling = next;
            }
        }

        // Keep walking up the chain.
        currentChild = current;
        current = downcast<RenderBoxModelObject>(current->parent());
        ++splitDepth;
    }

    // Clear the flow thread containing blocks cached during the detached state insertions.
    for (auto& cloneBlockChild : childrenOfType<RenderBlock>(*cloneInline))
        cloneBlockChild.resetEnclosingFragmentedFlowAndChildInfoIncludingDescendants();

    // Now we are at the block level. We need to put the clone into the toBlock.
    toBlock->insertChildInternal(WTFMove(cloneInline), nullptr);

    // Now take all the children after currentChild and remove them from the fromBlock
    // and put them in the toBlock.
    for (auto* current = currentChild->nextSibling(); current;) {
        auto* next = current->nextSibling();
        auto childToMove = fromBlock->takeChildInternal(*current);
        toBlock->insertChildInternal(WTFMove(childToMove), nullptr);
        current = next;
    }
}

bool RenderTreeBuilder::Inline::newChildIsInline(const RenderInline& parent, const RenderObject& child)
{
    // inline parent generates inline-table.
    return child.isInline() || (m_builder.tableBuilder().childRequiresTable(parent, child) && parent.style().display() == INLINE);
}

}
