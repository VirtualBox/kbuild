/* $Id: avl.h 2243 2009-01-10 02:24:02Z knut.osmundsen@oracle.com $
 *
 * AVL-Tree (lookalike) declaration.
 *
 * This AVL implementation is configurable from this headerfile. By
 * for example alterning the AVLKEY typedefinition an the AVL_<L|G|E|N>[E]
 * macros you are able to create different trees. Currently you may only have
 * one type of trees within one program (module).
 *
 * TREETYPE: Case Sensitive Strings (Key is pointer).
 *
 *
 * Copyright (c) 1999-2009 knut st. osmundsen
 *
 * GPL
 *
 */
#ifndef _AVL_H_
#define _AVL_H_

#ifdef __cplusplus
extern "C" {
#endif

/*
 * AVL configuration. PRIVATE!
 */
#define AVL_MAX_HEIGHT              19          /* Up to 2^16 nodes. */
#define AVL_MAY_TRY_INSERT_EQUAL    1           /* Ignore attempts to insert existing nodes. */

/*
 * AVL Compare macros
 */
#define AVL_L(key1, key2)  (strcmp(key1, key2) <  0)
#define AVL_LE(key1, key2) (strcmp(key1, key2) <= 0)
#define AVL_G(key1, key2)  (strcmp(key1, key2) >  0)
#define AVL_GE(key1, key2) (strcmp(key1, key2) >= 0)
#define AVL_E(key1, key2)  (strcmp(key1, key2) == 0)
#define AVL_NE(key1, key2) (strcmp(key1, key2) != 0)
#define AVL_CMP(key1, key2) strcmp(key1, key2)

/**
 * AVL key type
 */
typedef const char *AVLKEY;


/**
 * AVL Core node.
 */
typedef struct _AVLNodeCore
{
    AVLKEY                  Key;       /* Key value. */
    struct  _AVLNodeCore *  pLeft;     /* Pointer to left leaf node. */
    struct  _AVLNodeCore *  pRight;    /* Pointer to right leaf node. */
    unsigned char           uchHeight; /* Height of this tree: max(heigth(left), heigth(right)) + 1 */
} AVLNODECORE, *PAVLNODECORE, **PPAVLNODECORE;


/**
 * AVL Enum data - All members are PRIVATE! Don't touch!
 */
typedef struct _AVLEnumData
{
    char         fFromLeft;
    char         cEntries;
    char         achFlags[AVL_MAX_HEIGHT];
    PAVLNODECORE aEntries[AVL_MAX_HEIGHT];
} AVLENUMDATA, *PAVLENUMDATA;


/*
 * callback type
 */
typedef unsigned ( _PAVLCALLBACK)(PAVLNODECORE, void*);
typedef _PAVLCALLBACK *PAVLCALLBACK;


BOOL            AVLInsert(PPAVLNODECORE ppTree, PAVLNODECORE pNode);
PAVLNODECORE    AVLRemove(PPAVLNODECORE ppTree, AVLKEY Key);
PAVLNODECORE    AVLGet(PPAVLNODECORE ppTree, AVLKEY Key);
PAVLNODECORE    AVLGetWithParent(PPAVLNODECORE ppTree, PPAVLNODECORE ppParent, AVLKEY Key);
PAVLNODECORE    AVLGetWithAdjecentNodes(PPAVLNODECORE ppTree, AVLKEY Key, PPAVLNODECORE ppLeft, PPAVLNODECORE ppRight);
unsigned        AVLDoWithAll(PPAVLNODECORE ppTree, int fFromLeft, PAVLCALLBACK pfnCallBack, void *pvParam);
PAVLNODECORE    AVLBeginEnumTree(PPAVLNODECORE ppTree, PAVLENUMDATA pEnumData, int fFromLeft);
PAVLNODECORE    AVLGetNextNode(PAVLENUMDATA pEnumData);
PAVLNODECORE    AVLGetBestFit(PPAVLNODECORE ppTree, AVLKEY Key, int fAbove);



/*
 * Just in case NULL is undefined.
 */
#ifndef NULL
    #define NULL ((void*)0)
#endif

#ifdef __cplusplus
}
#endif

#endif
