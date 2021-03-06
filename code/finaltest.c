/* finaltest.c: LARGE-SCALE FINALIZATION TEST
 *
 * $Id$
 * Copyright (c) 2001-2014 Ravenbrook Limited.  See end of file for license.
 * Portions copyright (C) 2002 Global Graphics Software.
 *
 * DESIGN
 *
 * DEPENDENCIES
 *
 * This test uses the dylan object format, but the reliance on this
 * particular format is not great and could be removed.
 *
 * NOTES
 *
 * This code was created by first copying <code/finalcv.c>
 */

#include "testlib.h"
#include "mpslib.h"
#include "mps.h"
#include "mpscamc.h"
#include "mpscams.h"
#include "mpscawl.h"
#include "mpsclo.h"
#include "mpsavm.h"
#include "fmtdy.h"
#include "fmtdytst.h"
#include "mpstd.h"

#include <stdio.h> /* fflush, printf, stdout */


#define testArenaSIZE   ((size_t)16<<20)
#define rootCOUNT 20
#define maxtreeDEPTH 10
#define collectionCOUNT 10


/* global object counter */

static mps_word_t object_count = 0;

static mps_word_t make_numbered_cons(mps_word_t car, mps_word_t cdr,
                                     mps_ap_t ap)
{
    mps_word_t cons;
    die(make_dylan_vector(&cons, ap, 3), "make_dylan_vector");
    DYLAN_VECTOR_SLOT(cons, 0) = car;
    DYLAN_VECTOR_SLOT(cons, 1) = cdr;
    DYLAN_VECTOR_SLOT(cons, 2) = DYLAN_INT(object_count);
    ++ object_count;
    return cons;
}

static mps_word_t make_numbered_tree(mps_word_t depth,
                                     mps_ap_t ap)
{
    mps_word_t left, right;
    if (depth < 2) {
        left = DYLAN_INT(object_count);
        right = DYLAN_INT(object_count);
    } else {
        left = make_numbered_tree(depth-1, ap);
        right = make_numbered_tree(depth-1, ap);
    }
    return make_numbered_cons(left, right, ap);
}

static void register_numbered_tree(mps_word_t tree, mps_arena_t arena)
{
    /* don't finalize ints */
    if ((tree & 1) == 0) {
        mps_addr_t tree_ref = (mps_addr_t)tree;
        die(mps_finalize(arena, &tree_ref), "mps_finalize");
        register_numbered_tree(DYLAN_VECTOR_SLOT(tree, 0), arena);
        register_numbered_tree(DYLAN_VECTOR_SLOT(tree, 1), arena);
    }
}

static mps_word_t make_indirect_cons(mps_word_t car, mps_word_t cdr,
                                     mps_ap_t ap)
{
    mps_word_t cons, indirect;
    die(make_dylan_vector(&indirect, ap, 1), "make_dylan_vector");
    DYLAN_VECTOR_SLOT(indirect, 0) = DYLAN_INT(object_count);
    die(make_dylan_vector(&cons, ap, 3), "make_dylan_vector");
    DYLAN_VECTOR_SLOT(cons, 0) = car;
    DYLAN_VECTOR_SLOT(cons, 1) = cdr;
    DYLAN_VECTOR_SLOT(cons, 2) = indirect;
    ++ object_count;
    return cons;
}

static mps_word_t make_indirect_tree(mps_word_t depth,
                                     mps_ap_t ap)
{
    mps_word_t left, right;
    if (depth < 2) {
        left = DYLAN_INT(object_count);
        right = DYLAN_INT(object_count);
    } else {
        left = make_indirect_tree(depth-1, ap);
        right = make_indirect_tree(depth-1, ap);
    }
    return make_indirect_cons(left, right, ap);
}

static void register_indirect_tree(mps_word_t tree, mps_arena_t arena)
{
    /* don't finalize ints */
    if ((tree & 1) == 0) {
        mps_word_t indirect = DYLAN_VECTOR_SLOT(tree,2);
        mps_addr_t indirect_ref = (mps_addr_t)indirect;
        die(mps_finalize(arena, &indirect_ref), "mps_finalize");
        register_indirect_tree(DYLAN_VECTOR_SLOT(tree, 0), arena);
        register_indirect_tree(DYLAN_VECTOR_SLOT(tree, 1), arena);
    }
}

static mps_addr_t test_awl_find_dependent(mps_addr_t addr)
{
  testlib_unused(addr);
  return NULL;
}

static void *root[rootCOUNT];

static void test_trees(const char *name, mps_arena_t arena, mps_ap_t ap,
                       mps_word_t (*make)(mps_word_t, mps_ap_t),
                       void (*reg)(mps_word_t, mps_arena_t))
{
  size_t collections = 0;
  size_t finals = 0;
  size_t i;

  object_count = 0;

  printf("Making some %s finalized trees of objects.\n", name);
  /* make some trees */
  for(i = 0; i < rootCOUNT; ++i) {
    root[i] = (void *)(*make)(maxtreeDEPTH, ap);
    (*reg)((mps_word_t)root[i], arena);
  }

  printf("Losing all pointers to the trees.\n");
  /* clean out the roots */
  for(i = 0; i < rootCOUNT; ++i) {
    root[i] = 0;
  }

  while (finals < object_count && collections < collectionCOUNT) {
    mps_word_t final_this_time = 0;
    printf("Collecting...");
    (void)fflush(stdout);
    die(mps_arena_collect(arena), "collect");
    printf(" Done.\n");
    ++ collections;
    while (mps_message_poll(arena)) {
      mps_message_t message;
      mps_addr_t objaddr;
      cdie(mps_message_get(&message, arena, mps_message_type_finalization()),
           "message_get");
      mps_message_finalization_ref(&objaddr, arena, message);
      mps_message_discard(arena, message);
      ++ final_this_time;
    }
    finals += final_this_time;
    printf("%"PRIuLONGEST" objects finalized: total %"PRIuLONGEST
           " of %"PRIuLONGEST"\n", (ulongest_t)final_this_time,
           (ulongest_t)finals, (ulongest_t)object_count);
  }
  cdie(finals == object_count, "Not all objects were finalized.");
}

static void *test(mps_arena_t arena, mps_class_t pool_class)
{
  mps_ap_t ap;
  mps_fmt_t fmt;
  mps_pool_t pool;
  mps_root_t mps_root;

  die(mps_fmt_create_A(&fmt, arena, dylan_fmt_A()), "fmt_create\n");
  MPS_ARGS_BEGIN(args) {
    /* Allocate into generation 0 so that they get finalized quickly. */
    MPS_ARGS_ADD(args, MPS_KEY_GEN, 0);
    MPS_ARGS_ADD(args, MPS_KEY_FORMAT, fmt);
    MPS_ARGS_ADD(args, MPS_KEY_AWL_FIND_DEPENDENT, test_awl_find_dependent);
    die(mps_pool_create_k(&pool, arena, pool_class, args),
        "pool_create\n");
  } MPS_ARGS_END(args);
  die(mps_root_create_table(&mps_root, arena, mps_rank_exact(), (mps_rm_t)0,
                            root, (size_t)rootCOUNT),
      "root_create\n");
  die(mps_ap_create(&ap, pool, mps_rank_exact()), "ap_create\n");

  mps_message_type_enable(arena, mps_message_type_finalization());

  mps_arena_park(arena);

  test_trees("numbered", arena, ap, make_numbered_tree, register_numbered_tree);
  test_trees("indirect", arena, ap, make_indirect_tree, register_indirect_tree);

  mps_ap_destroy(ap);
  mps_root_destroy(mps_root);
  mps_pool_destroy(pool);
  mps_fmt_destroy(fmt);

  return NULL;
}


int main(int argc, char *argv[])
{
  mps_arena_t arena;
  mps_thr_t thread;

  testlib_init(argc, argv);

  die(mps_arena_create(&arena, mps_arena_class_vm(), testArenaSIZE),
      "arena_create\n");
  die(mps_thread_reg(&thread, arena), "thread_reg\n");

  test(arena, mps_class_amc());
  test(arena, mps_class_amcz());
  test(arena, mps_class_ams());
  test(arena, mps_class_awl());
  /* TODO: test(arena, mps_class_lo()); */

  mps_thread_dereg(thread);
  mps_arena_destroy(arena);

  printf("%s: Conclusion: Failed to find any defects.\n", argv[0]);
  return 0;
}


/* C. COPYRIGHT AND LICENSE
 *
 * Copyright (c) 2001-2014 Ravenbrook Limited <http://www.ravenbrook.com/>.
 * All rights reserved.  This is an open source license.  Contact
 * Ravenbrook for commercial licensing options.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * 3. Redistributions in any form must be accompanied by information on how
 * to obtain complete source code for this software and any accompanying
 * software that uses this software.  The source code must either be
 * included in the distribution or be available for no more than the cost
 * of distribution plus a nominal fee, and must be freely redistributable
 * under reasonable conditions.  For an executable file, complete source
 * code means the source code for all modules it contains. It does not
 * include source code for modules or files that typically accompany the
 * major components of the operating system on which the executable file
 * runs.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR
 * PURPOSE, OR NON-INFRINGEMENT, ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS AND CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
