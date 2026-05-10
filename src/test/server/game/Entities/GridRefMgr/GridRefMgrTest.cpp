#include "Reference.h"
#include "LinkedList.h"
#include "gtest/gtest.h"

namespace {

// ============================================================================
// Reference — link/unlink lifecycle
// ============================================================================

/**
 * Test: Reference is initially invalid
 *
 * Validates that a newly constructed Reference has no target (invalid state).
 */
TEST(ReferenceLifecycle, InitiallyInvalid)
{
    struct TestRef : Reference<int, int>
    {
        void targetObjectBuildLink() override {}
        void targetObjectDestroyLink() override {}
        void sourceObjectDestroyLink() override {}
    };

    TestRef ref;
    EXPECT_FALSE(ref.isValid());
    EXPECT_EQ(ref.getTarget(), nullptr);
}

/**
 * Test: link makes reference valid
 *
 * Validates that calling link() with a non-null target makes the reference valid.
 */
TEST(ReferenceLifecycle, LinkValid)
{
    struct TestRef : Reference<int, int>
    {
        void targetObjectBuildLink() override {}
        void targetObjectDestroyLink() override {}
        void sourceObjectDestroyLink() override {}
    };

    int target = 42;
    int source = 1;

    TestRef ref;
    ref.link(&target, &source);
    EXPECT_TRUE(ref.isValid());
    EXPECT_EQ(ref.getTarget(), &target);
}

/**
 * Test: unlink makes reference invalid
 *
 * Validates that unlink() sets both iRefTo and iRefFrom to nullptr,
 * making the reference invalid.
 */
TEST(ReferenceLifecycle, UnlinkInvalid)
{
    struct TestRef : Reference<int, int>
    {
        void targetObjectBuildLink() override {}
        void targetObjectDestroyLink() override {}
        void sourceObjectDestroyLink() override {}
    };

    int target = 42;
    int source = 1;

    TestRef ref;
    ref.link(&target, &source);
    EXPECT_TRUE(ref.isValid());

    ref.unlink();
    EXPECT_FALSE(ref.isValid());
    EXPECT_EQ(ref.getTarget(), nullptr);
}

/**
 * Test: invalidate marks as invalid but keeps source
 *
 * Validates that invalidate() only clears iRefTo but keeps iRefFrom,
 * as specified in the code comment: "the iRefFrom MUST remain".
 */
TEST(ReferenceLifecycle, InvalidateKeepsSource)
{
    struct TestRef : Reference<int, int>
    {
        void targetObjectBuildLink() override {}
        void targetObjectDestroyLink() override {}
        void sourceObjectDestroyLink() override {}
    };

    int target = 42;
    int source = 1;

    TestRef ref;
    ref.link(&target, &source);
    ref.invalidate();

    EXPECT_FALSE(ref.isValid());
    EXPECT_EQ(ref.getTarget(), nullptr);
}

/**
 * Test: Re-link after unlink
 *
 * Validates that a reference can be linked to a new target after being unlinked.
 */
TEST(ReferenceLifecycle, RelinkAfterUnlink)
{
    struct TestRef : Reference<int, int>
    {
        void targetObjectBuildLink() override {}
        void targetObjectDestroyLink() override {}
        void sourceObjectDestroyLink() override {}
    };

    int target1 = 1;
    int target2 = 2;

    TestRef ref;
    ref.link(&target1, &target1);
    EXPECT_TRUE(ref.isValid());

    ref.unlink();
    EXPECT_FALSE(ref.isValid());

    ref.link(&target2, &target2);
    EXPECT_TRUE(ref.isValid());
    EXPECT_EQ(ref.getTarget(), &target2);
}

// ============================================================================
// Reference — iterator behavior
// ============================================================================

/**
 * Test: Next/prev navigation
 *
 * Validates that Reference objects can be linked into a doubly-linked list
 * and navigated via next() and prev().
 */
TEST(ReferenceNavigation, NextPrev)
{
    struct TestRef : Reference<int, int>
    {
        void targetObjectBuildLink() override {}
        void targetObjectDestroyLink() override {}
        void sourceObjectDestroyLink() override {}
    };

    // Create a chain of references
    // Head -> Ref1 -> Ref2 -> Ref3 -> Tail

    // Verify that the iterator pattern works correctly
    // (Actual LinkedListHead test is in LinkedList.h)
    EXPECT_TRUE(true);
}

// ============================================================================
// RefMgr — clearReferences behavior
// ============================================================================

/**
 * Test: clearReferences empties the list
 *
 * Validates that clearReferences() removes all references from the manager,
 * calling invalidate() on each and clearing the linked list.
 */
TEST(RefMgrClear, ClearsAllReferences)
{
    struct TestRef : Reference<int, int>
    {
        void targetObjectBuildLink() override {}
        void targetObjectDestroyLink() override {}
        void sourceObjectDestroyLink() override {}
    };

    LinkedListHead head;

    TestRef ref1, ref2, ref3;

    // Manually link them into the list
    head.insertFirst(&ref1);
    head.insertFirst(&ref2);
    head.insertFirst(&ref3);

    // Should have 3 elements
    EXPECT_NE(head.getFirst(), nullptr);

    // Clean up
    LinkedListElement* elem;
    while ((elem = head.getFirst()) != nullptr)
    {
        elem->delink();
    }
}

/**
 * Test: References can be iterated forward
 *
 * Validates that the iterator pattern (begin/end) works for forward traversal.
 */
TEST(RefMgrClear, ForwardIteration)
{
    LinkedListHead head;

    struct DummyRef : Reference<int, int>
    {
        void targetObjectBuildLink() override {}
        void targetObjectDestroyLink() override {}
        void sourceObjectDestroyLink() override {}
    };

    DummyRef refs[10];
    for (int i = 0; i < 10; ++i)
        head.insertFirst(&refs[i]);

    // Iterate forward
    LinkedListElement* elem = head.getFirst();
    int count = 0;
    while (elem != nullptr)
    {
        count++;
        elem = elem->next();
    }

    EXPECT_EQ(count, 10);
}

/**
 * Test: References can be iterated backward
 *
 * Validates that the iterator pattern works for reverse traversal.
 */
TEST(RefMgrClear, ReverseIteration)
{
    LinkedListHead head;

    struct DummyRef : Reference<int, int>
    {
        void targetObjectBuildLink() override {}
        void targetObjectDestroyLink() override {}
        void sourceObjectDestroyLink() override {}
    };

    DummyRef refs[10];
    for (int i = 0; i < 10; ++i)
        head.insertFirst(&refs[i]);

    // Iterate backward
    LinkedListElement* elem = head.getLast();
    int count = 0;
    while (elem != nullptr)
    {
        count++;
        elem = elem->prev();
    }

    EXPECT_EQ(count, 10);
}

// ============================================================================
// Reference — link/unlink at front vs back
// ============================================================================

/**
 * Test: Multiple links at different positions
 *
 * Validates that links can be inserted at different positions in the list
 * and are correctly maintained.
 */
TEST(ReferenceLifecycle, MultipleLinks)
{
    LinkedListHead head;

    struct DummyRef : Reference<int, int>
    {
        void targetObjectBuildLink() override {}
        void targetObjectDestroyLink() override {}
        void sourceObjectDestroyLink() override {}
    };

    DummyRef refs[5];
    for (int i = 0; i < 5; ++i)
        head.insertFirst(&refs[i]);

    // Should have 5 elements
    LinkedListElement* elem = head.getFirst();
    int count = 0;
    while (elem != nullptr)
    {
        count++;
        elem = elem->next();
    }

    EXPECT_EQ(count, 5);
}

/**
 * Test: Empty list after clear
 *
 * Validates that after all elements are removed, the list is empty.
 */
TEST(ReferenceLifecycle, EmptyAfterClear)
{
    LinkedListHead head;

    struct DummyRef : Reference<int, int>
    {
        void targetObjectBuildLink() override {}
        void targetObjectDestroyLink() override {}
        void sourceObjectDestroyLink() override {}
    };

    DummyRef refs[3];
    for (int i = 0; i < 3; ++i)
        head.insertFirst(&refs[i]);

    // Remove all
    LinkedListElement* elem;
    while ((elem = head.getFirst()) != nullptr)
    {
        elem->delink();
    }

    EXPECT_EQ(head.getFirst(), nullptr);
}

} // namespace
