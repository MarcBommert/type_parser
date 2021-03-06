#include "stdafx.h"

#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>

#include <Windows.h>    /* for WideCharToMultiByte() */

#include "clang-c/Index.h"

#pragma comment(lib, "libclang.lib")

#define MAX_CLANG_ARGUMENTS 255 /* Maximum number of CL arguments we can hand over to clang */

/* terminal window output current indendation */
#define MAX_INDENT 32

static int indent = 0;
static char szIndent[MAX_INDENT];

static void indentIncr()
{
  if (indent <= MAX_INDENT)
    szIndent[indent++] = ' ';
}

static void indentDecr()
{
  szIndent[--indent] = '\0';
}

/* very simple singly linked list with push_back only. No remove. */
typedef struct my_queue_elem_str
{
  struct my_queue_elem_str *ptNext;
} QUEUE_ELEM_T;

typedef struct my_queue_head_str
{
  QUEUE_ELEM_T *ptFirst;
  QUEUE_ELEM_T *ptLast;
  unsigned int numElems;
} QUEUE_HEAD_T;

void addQueueElement(QUEUE_HEAD_T* ptHead, QUEUE_ELEM_T* ptElem)
{
  ptElem->ptNext = NULL;
  if (ptHead->ptFirst == NULL)
  {
    ptHead->ptFirst = ptElem;
  }
  else
  {
    ptHead->ptLast->ptNext = ptElem;
  }
  ptHead->ptLast = ptElem;
  ptHead->numElems++;
}

QUEUE_ELEM_T *queueIterBegin(QUEUE_HEAD_T *ptHead)
{
  return ptHead->ptFirst;
}

bool queueIterHasNext(QUEUE_ELEM_T *ptIter)
{
  return ((ptIter != NULL));
}

QUEUE_ELEM_T *queueIterNext(QUEUE_ELEM_T *ptIter)
{
  return ptIter->ptNext;
}


/* A type represents a type as parsed from the AST.  */
typedef struct typeTAG
{
  QUEUE_ELEM_T tElem;   /* Queue element. This must be the first member in this structure.*/
  char* abTypeName;     /* type name */
  char* abMemberName;   /* for members of structs, enums, unions: member name */
  uint32_t iSize;       /* size of the type in bytes */
  uint32_t iAlignment;  /* alignment of the type in bytes */

  /* type kind */
  enum
  {
    SIMPLE = 0,     /* simple types don't have children, interpretation of data representation is by type name */
    STRUCT = 1,     /* children specify structure members */
    UNION = 2,      /* children speciy union members */
    ENUM = 3,       /* children specify enum mebers */
    ARRAY = 4,      /* for arrays we denote the element base type in atChildren[0] */
  } eKind;

  uint32_t fIsConstValue;       /* is a constant value assigned?  */
  int64_t iConstValue;          /* for enum constants */
  
  QUEUE_HEAD_T tChildren;       /* list of children */

} type_t;

/* A define represents a #define we parse from the tokenized translation unit */
typedef struct defineTAG
{
  QUEUE_ELEM_T tElem;   /* Queue element. This must be the first member in this structure.*/
  char *abIdentifier;   /* name of the #define */
  char *abLiteral;      /* value of the #define */
} define_t;

/* global list of type we build up during AST parsing */
static QUEUE_HEAD_T typeList;

/* List of preprocessor defines we parse */
static QUEUE_HEAD_T defineList;

/* intermediate pointer to rmeember the parent of the currently processed type */
type_t* gParent = NULL;

/* Add a parsed defined to the list of defines */
static define_t* addDefine(const char* name, const char* value)
{
  
  if (strlen(value) == 0)
    return NULL;

  printf("add #define %s %s\n", name, value);

  define_t *dadd = (define_t*)malloc(sizeof(define_t));
  memset(dadd, 0, sizeof(*dadd));
  dadd->abIdentifier = (char*)malloc(strlen(name) + 1);
  dadd->abLiteral = (char*)malloc(strlen(value) + 1);
  strncpy(dadd->abIdentifier, name, strlen(name) + 1);
  strncpy(dadd->abLiteral, value, strlen(value) + 1);

  addQueueElement(&defineList, &dadd->tElem);  
  return dadd;
}

/*
* If parent is specified as NULL, then add the given type "t" to the list of known types
* Otherwise, add the given type "t" as a child of the given parent type.
* The function will, in any case, create a deep-copy of "t" and return a pointer to this copy,
* which is the actual pointer enqueued into the type list (or added to the list of children)
*/
static type_t* addType(
  const char* type_name,
  const char* member_name,
  type_t* t,
  type_t* parent)
{
  type_t *tadd = (type_t*)malloc(sizeof(*t));
  memset(tadd, 0, sizeof(*tadd));

  /* copy-in */
  memcpy(tadd, t, sizeof(*t));
  tadd->abTypeName = (char*)malloc(strlen(type_name) + 1);
  strncpy(tadd->abTypeName, type_name, strlen(type_name) + 1);

  tadd->abMemberName = NULL;
  if (member_name != NULL)
  {
    tadd->abMemberName = (char*)malloc(strlen(member_name) + 1);
    strncpy(tadd->abMemberName, member_name, strlen(member_name) + 1);
  }

  if (parent == NULL)
  {
    /* add as global type */

    /* check if we already know the type by name */
#if 0 
    for (type_t *ptType = (type_t*)queueIterBegin(&typeList); queueIterHasNext(&ptType->tElem); ptType = (type_t*)queueIterNext(&ptType->tElem))
    {      
      if (strcmp(tadd->abTypeName, ptType->abTypeName) == 0)
      {
        /* type already known */
        //printf("already known: %s\n", tadd->abTypeName);
        free(tadd->abTypeName);
        free(tadd);
        return NULL;
      }
    }
#endif
    //printf("adding new type %s\n", tadd->abTypeName);
    addQueueElement(&typeList, &tadd->tElem);
  }
  else {
    /* add as child of parent */
    printf("Adding %s as child of %s (ref %p)\n", tadd->abTypeName, parent->abTypeName, parent);
    addQueueElement(&parent->tChildren, &tadd->tElem);
  }
  return tadd;
}


/*
* Recursively dump the type tree for the given type "t" in human readable form.
*/
static void dump_type(type_t* t, int depth)
{
  const char* szKind = "UNKNOWN";
  switch (t->eKind)
  {
  case t->SIMPLE:
    szKind = "SIMPLE";
    break;
  case t->ENUM:
    szKind = "ENUM";
    break;
  case t->UNION:
    szKind = "UNION";
    break;
  case t->STRUCT:
    szKind = "STRUCT";
    break;
  case t->ARRAY:
    szKind = "ARRAY";
    break;
  }
  const char* szMemberName = "";
  if (t->abMemberName != NULL)
    szMemberName = t->abMemberName;

  if (t->fIsConstValue)
    printf("%s%s type \"%s\" of size %u, align %u, member \"%s\", value %lld\n", szIndent, szKind, t->abTypeName, t->iSize, t->iAlignment, szMemberName, t->iConstValue);
  else
    printf("%s%s type \"%s\" of size %u, align %u, member \"%s\"\n", szIndent, szKind, t->abTypeName, t->iSize, t->iAlignment, szMemberName);

  for (type_t *ptChild = (type_t*)queueIterBegin(&t->tChildren); queueIterHasNext(&ptChild->tElem); ptChild = (type_t*)queueIterNext(&ptChild->tElem))
  {
    indentIncr();
    dump_type(ptChild, depth + 1);
    indentDecr();
  }
}

/* for all types in the type list: Recursively dump each type's tree. */
static void dump_type_db()
{
  for (type_t *ptType = (type_t*)queueIterBegin(&typeList); queueIterHasNext(&ptType->tElem); ptType = (type_t*)queueIterNext(&ptType->tElem))
  {
    dump_type(ptType, 0);
  }
}

/*
* Recursively serialize the given type "t" into the filestream give as "fout"
*/
static void serialize_type(type_t* t, FILE* fout)
{
  const char* szKind = "UNKNOWN";
  switch (t->eKind)
  {
  case t->SIMPLE:
    szKind = "SIMPLE";
    break;
  case t->ENUM:
    szKind = "ENUM";
    break;
  case t->UNION:
    szKind = "UNION";
    break;
  case t->STRUCT:
    szKind = "STRUCT";
    break;
  case t->ARRAY:
    szKind = "ARRAY";
    break;
  }

  const char* szMemberName = "";
  if (t->abMemberName != NULL)
    szMemberName = t->abMemberName;

  fwrite(szMemberName, 1, strlen(szMemberName) + 1, fout);
  fflush(fout);
  fwrite(t->abTypeName, 1, strlen(t->abTypeName) + 1, fout);
  fflush(fout);
  fwrite(&t->eKind, sizeof(t->eKind), 1, fout);
  fflush(fout);
  fwrite(&t->iSize, sizeof(t->iSize), 1, fout);
  fflush(fout);
  fwrite(&t->iAlignment, sizeof(t->iAlignment), 1, fout);
  fflush(fout);
  fwrite(&t->fIsConstValue, sizeof(t->fIsConstValue), 1, fout);
  fflush(fout);
  fwrite(&t->iConstValue, sizeof(t->iConstValue), 1, fout);
  fflush(fout);
  fwrite(&t->tChildren.numElems, sizeof(t->tChildren.numElems), 1, fout);
  fflush(fout);

  //printf("%s%s type \"%s\" of size %d, align %d, member \"%s\", value %d\n", szIndent, szKind, t->abTypeName, t->iSize, t->iAlignment, szMemberName, t->iConstValue);
  for (type_t *ptChild = (type_t*)queueIterBegin(&t->tChildren); queueIterHasNext(&ptChild->tElem); ptChild = (type_t*)queueIterNext(&ptChild->tElem))
  {
    serialize_type(ptChild, fout);
  }
}

/* for all defines in the define list: Write them to the output file */
static void serialize_define_db(FILE* fout)
{
  unsigned int magic = 0x12021984;
  fwrite(&magic, sizeof(magic), 1, fout);
  unsigned int numDefines = defineList.numElems;
  fwrite(&numDefines, sizeof(numDefines), 1, fout);

  for (define_t *ptDefine = (define_t*)queueIterBegin(&defineList); queueIterHasNext(&ptDefine->tElem); ptDefine = (define_t*)queueIterNext(&ptDefine->tElem))
  {
    fwrite(ptDefine->abIdentifier, 1, strlen(ptDefine->abIdentifier) + 1, fout);
    fwrite(ptDefine->abLiteral, 1, strlen(ptDefine->abLiteral) + 1, fout);
  }
}

/* for all types in the type list: Recursively serialize each type's tree into the file given by name "szOutFile". */
static void serialize_type_db(FILE* fout)
{
  unsigned int magic = 0x23c0ffee;
  fwrite(&magic, sizeof(magic), 1, fout);
  unsigned int numTypes = typeList.numElems;
  fwrite(&numTypes, sizeof(numTypes), 1, fout);
  for (type_t *ptType = (type_t*)queueIterBegin(&typeList); queueIterHasNext(&ptType->tElem); ptType = (type_t*)queueIterNext(&ptType->tElem))
  {
    serialize_type(ptType, fout);
  }
}

/* Check the given return code for certain well-known libclang error codes and emit an error message on match */
static void check_type_layout_error(long long err)
{
  if (err == CXTypeLayoutError_Invalid)
  {
    printf("Invalid type\n");
  }
  else if (err == CXTypeLayoutError_Incomplete)
  {
    printf("Incomplete type\n");
  }
  else if (err == CXTypeLayoutError_Dependent)
  {
    printf("Dependent type\n");
  }
  else if (err == CXTypeLayoutError_NotConstantSize)
  {
    printf("Not constant size type\n");
  }
  else if (err == CXTypeLayoutError_InvalidFieldName)
  {
    printf("Invalid field name\n");
  }
}

/* prototypes */
static CXChildVisitResult myVisitor(CXCursor cursor,
  CXCursor parent,
  CXClientData client_data);

static CXChildVisitResult myTypedefChildrenVisitor(CXCursor cursor,
  CXCursor parent,
  CXClientData client_data);

/* parse type and add to type list */
static void handleType(CXCursor cursor, const char* name, CXType type, type_t* parent)
{
  long long type_size = clang_Type_getSizeOf(type);
  check_type_layout_error(type_size);
  long long type_align = clang_Type_getAlignOf(type);
  check_type_layout_error(type_align);
  const char* typeSpelling = clang_getCString(clang_getTypeSpelling(type));

  type_t t;
  memset(&t, 0, sizeof(t));
  t.eKind = t.SIMPLE;
  t.iSize = (int)type_size;
  t.iAlignment = (int)type_align;

  const char* structName = clang_getCString(clang_getCursorSpelling(cursor));

  if (cursor.kind == CXCursor_EnumConstantDecl)
  {
    /* clang_getEnumConstantDeclValue() never fails */
    long long enumDeclConstValue = clang_getEnumConstantDeclValue(cursor);
    printf("enumdeclconstvalue = %lld\n", enumDeclConstValue);

    t.iConstValue = enumDeclConstValue;
    t.fIsConstValue = 1;
  }
  else
  {
    t.fIsConstValue = 0;
  }

  switch (type.kind)
  {
  case CXType_Invalid: printf("CXType_Invalid\n"); break;
  case CXType_Unexposed: printf("CXType_Unexposed\n"); break;

    /* Builtin types */
  case CXType_Void: printf("CXType_Void\n");
    t.iSize = 0;
    t.iAlignment = 0;
    addType("Void", structName, &t, parent);

    break;
  case CXType_Bool: printf("CXType_Bool\n");
    addType("Bool", structName, &t, parent);
    break;
  case CXType_Char_U: printf("CXType_Char_U\n");
    addType("Char", structName, &t, parent);
    break;
  case CXType_UChar: printf("CXType_UChar\n");
    addType("UChar", structName, &t, parent);
    break;
  case CXType_Char16: printf("CXType_Char16\n");
    addType("Char16", structName, &t, parent);
    break;
  case CXType_Char32: printf("CXType_Char32\n");
    addType("Char32", structName, &t, parent);
    break;
  case CXType_UShort: printf("CXType_UShort\n");
    addType("UShort", structName, &t, parent);
    break;
  case CXType_UInt: printf("CXType_UInt\n");
    addType("UInt", structName, &t, parent);
    break;
  case CXType_ULong: printf("CXType_ULong\n");
    addType("ULong", structName, &t, parent);
    break;
  case CXType_ULongLong: printf("CXType_ULongLong\n");
    addType("ULongLong", structName, &t, parent);
    break;
  case CXType_UInt128: printf("CXType_UInt128\n");
    addType("UInt128", structName, &t, parent);
    break;
  case CXType_Char_S: printf("CXType_Char_S\n");
    addType("Char_S", structName, &t, parent);
    break;
  case CXType_SChar: printf("CXType_SChar\n");
    addType("SChar", structName, &t, parent);
    break;
  case CXType_WChar: printf("CXType_WChar\n");
    addType("WChar", structName, &t, parent);
    break;
  case CXType_Short: printf("CXType_Short\n");
    addType("Short", structName, &t, parent);
    break;
  case CXType_Int: printf("CXType_Int\n");
    addType("Int", structName, &t, parent);
    break;
  case CXType_Long: printf("CXType_Long\n");
    addType("Long", structName, &t, parent);
    break;
  case CXType_LongLong: printf("CXType_LongLong\n");
    addType("LongLong", structName, &t, parent);
    break;
  case CXType_Int128: printf("CXType_Int128\n");
    addType("Int128", structName, &t, parent);
    break;
  case CXType_Float: printf("CXType_Float\n");
    addType("Float", structName, &t, parent);
    break;
  case CXType_Double: printf("CXType_Double\n");
    addType("Double", structName, &t, parent);
    break;
  case CXType_LongDouble: printf("CXType_LongDouble\n");
    addType("LongDouble", structName, &t, parent);
    break;
  case CXType_NullPtr: printf("CXType_NullPtr\n"); break;
  case CXType_Overload: printf("CXType_Overload\n"); break;
  case CXType_Dependent: printf("CXType_Dependent\n"); break;
  case CXType_ObjCId: printf("CXType_ObjCId\n"); break;
  case CXType_ObjCClass: printf("CXType_ObjCClass\n"); break;
  case CXType_ObjCSel: printf("CXType_ObjCSel\n"); break;
  case CXType_Float128: printf("CXType_Float128\n"); break;

  case CXType_Complex: printf("CXType_Complex\n"); break;
  case CXType_Pointer: printf("CXType_Pointer\n");
    addType("Pointer", structName, &t, parent);
    break;

  case CXType_BlockPointer: printf("CXType_BlockPointer\n"); break;
  case CXType_LValueReference: printf("CXType_LValueReference\n"); break;
  case CXType_RValueReference: printf("CXType_RValueReference\n"); break;
  case CXType_Record: printf("CXType_Record\n"); break;
  case CXType_Enum: printf("CXType_Enum\n");
    break;
  case CXType_Typedef: printf("CXType_Typedef\n");
  {
    /* Since we already triggered on a typedef , this is probably a field
    * declaration of type typedef.
    */
    CXType _t = clang_getTypedefDeclUnderlyingType(cursor);
    CXCursor c = clang_getTypeDeclaration(_t);

    type_t* gParentPrev = gParent;
    gParent = addType(typeSpelling, structName, &t, parent);
    indentIncr();
    clang_visitChildren(cursor, myVisitor, NULL);
    indentDecr();
    gParent = gParentPrev;
  }
  break;
  case CXType_ObjCInterface: printf("CXType_ObjCInterface\n"); break;
  case CXType_ObjCObjectPointer: printf("CXType_ObjCObjectPointer\n"); break;
  case CXType_FunctionNoProto: printf("CXType_FunctionNoProto\n"); break;
  case CXType_FunctionProto: printf("CXType_FunctionProto\n"); break;
  case CXType_ConstantArray: printf("CXType_ConstantArray\n");
  {
    long long arraySize = clang_getArraySize(type);
    CXType elementType = clang_getArrayElementType(type);

    const char* elementTypeSpelling = clang_getCString(clang_getTypeSpelling(elementType));
    printf("Array of size %lld, base type %s\n", arraySize, elementTypeSpelling);
    t.eKind = t.ARRAY;

    CXCursor c = clang_getTypeDeclaration(elementType);
    type_t* gParentPrev = gParent;
    gParent = addType(name, structName, &t, parent);
    const char* typeName = clang_getCString(clang_getCursorSpelling(c));
    handleType(c, typeName, elementType, gParent);
    gParent = gParentPrev;

  }
  break;
  case CXType_Vector: printf("CXType_Vector\n"); break;
  case CXType_IncompleteArray: printf("CXType_IncompleteArray\n"); break;
  case CXType_VariableArray: printf("CXType_VariableArray\n"); break;
  case CXType_DependentSizedArray: printf("CXType_DependentSizedArray\n"); break;
  case CXType_MemberPointer: printf("CXType_MemberPointer\n"); break;
  case CXType_Auto: printf("CXType_Auto\n"); break;

  /**
  * An elaborated type is either a struct, union or enum. These are the types we are mostly
  * interested in. Add the type definition to our types list and process all children
  */
  case CXType_Elaborated:
  {
    printf("CXType_Elaborated, ");

    CXCursor c = clang_getTypeDeclaration(type);
    CXCursorKind typeDeclKind = clang_getCursorKind(c);
    const char* typeDeclKindSpelling = clang_getCString(clang_getCursorKindSpelling(typeDeclKind));
    printf("typeDeclKind: %s\n", typeDeclKindSpelling);

    switch (typeDeclKind)
    {
    case CXCursor_EnumDecl:
      t.eKind = t.ENUM;
      break;
    case CXCursor_StructDecl:
      t.eKind = t.STRUCT;
      break;
    case CXCursor_UnionDecl:
      t.eKind = t.UNION;
      break;
    }

    type_t* gParentPrev = gParent;
    gParent = addType(typeSpelling, structName, &t, parent);
    indentIncr();
    clang_visitChildren(c, myTypedefChildrenVisitor, NULL);
    indentDecr();
    gParent = gParentPrev;
  }
  break;
  }
}

/*
* Called for each elaborated typedef
*/
static CXChildVisitResult myTypedefChildrenVisitor(
  CXCursor cursor,
  CXCursor parent,
  CXClientData client_data)
{
  /* cursor kind */
  CXCursorKind kind = clang_getCursorKind(cursor);
  const char* kindSpelling = clang_getCString(clang_getCursorKindSpelling(kind));

  /* cursor name */
  const char* structName = clang_getCString(clang_getCursorSpelling(cursor));
  if (strcmp(structName, "") == 0)
    return CXChildVisit_Continue;

  /* cursor type */
  CXType type = clang_getCursorType(cursor);
  const char* typeKindSpelling = clang_getCString(clang_getTypeKindSpelling(type.kind));

  long long type_size = clang_Type_getSizeOf(type);
  check_type_layout_error(type_size);
  long long type_align = clang_Type_getAlignOf(type);
  check_type_layout_error(type_align);

  const char* typeSpelling = clang_getCString(clang_getTypeSpelling(type));

  printf("%sChild: %s, cursorkind %s, typekind %s, typeName = %s\n", szIndent, structName, kindSpelling, typeKindSpelling, typeSpelling);
  
  indentIncr();
  /* for all typedefs: pass cursor and corresponding type, type name and parent (containing) type */
  handleType(cursor, structName, type, gParent);
  indentDecr();

  return CXChildVisit_Continue;
}

/* Will be called back by libclang for each node in the AST.
* This triggers on all typedefs and calls "handleType" for them, +
* passing the current cursor, its spelling, the underlying type and
* teh parent type (whcih may be NULL for root types) */
static CXChildVisitResult myVisitor(
  CXCursor cursor,
  CXCursor parent,
  CXClientData client_data)
{
  /* cursor kind */
  CXCursorKind kind = clang_getCursorKind(cursor);

  /* cursor name */
  const char* structName = clang_getCString(clang_getCursorSpelling(cursor));

  /* trigger on typedefs */
  if (kind == CXCursor_TypedefDecl)
  {
    CXType type = clang_getTypedefDeclUnderlyingType(cursor);
    printf("%stypedef \"%s\":\n", szIndent, structName);
    handleType(cursor, structName, type, gParent);
    printf("--- end of typedef ---\n");
  }
  return CXChildVisit_Continue;
}


static void parsePreprocessorDefines(CXTranslationUnit tu)
{
  CXToken* tokens;
  unsigned int num_tokens;
  CXSourceRange range = clang_getCursorExtent(clang_getTranslationUnitCursor(tu));
  clang_tokenize(tu, range, &tokens, &num_tokens);

  int state = 0;
  const char* identifier = "";
  char value[1024];
  int valueIndex = 0;


  for (unsigned int i = 0; i < num_tokens; ++i) {
    CXToken token = tokens[i];
    switch (clang_getTokenKind(token)) {
    case CXToken_Punctuation:
      if (clang_getCString(clang_getTokenSpelling(tu, token))[0] == '#')
      {
        if (state == 3)
        {
          value[valueIndex] = '\0';
          addDefine(identifier, value);
          state = 0;
          valueIndex = 0;
        }

        if (state == 0) state = 1;
      }
      else if (state != 0)
      {
        const char* p = clang_getCString(clang_getTokenSpelling(tu, token));
        memcpy(&value[valueIndex], p, strlen(p));
        valueIndex += strlen(p);
      }
      break;
    case CXToken_Keyword:
      state = 0;
      valueIndex = 0;
      break;
    case CXToken_Identifier:
      if (strcmp(clang_getCString(clang_getTokenSpelling(tu, token)), "define") == 0)
      {
        if (state == 1) state = 2;
      }
      else if (state == 2)
      {
        identifier = clang_getCString(clang_getTokenSpelling(tu, token));
        state = 3;
      }
      else if (state == 3)
      {
        const char* i = clang_getCString(clang_getTokenSpelling(tu, token));
        memcpy(&value[valueIndex], i, strlen(i));
        valueIndex += strlen(i);
      }
      else
      {
        state = 0;
        valueIndex = 0;
      }
      break;
    case CXToken_Literal:
      if (state == 3)
      {
        const char* v = clang_getCString(clang_getTokenSpelling(tu, token));
        memcpy(&value[valueIndex], v, strlen(v));
        valueIndex += strlen(v);
        //value[valueIndex] = '\0';
        //addDefine(identifier, value);
      }
      //state = 0;
      //valueIndex = 0;
      break;
    case CXToken_Comment:
      break;
    default:
      state = 0;
      valueIndex = 0;
      break;
    }
  }
  if (state == 3)
  {
    value[valueIndex] = '\0';
    addDefine(identifier, value);
    state = 0;
    valueIndex = 0;
  }
}

typedef struct {
  char **filenames;
  unsigned num_files;
} ImportedASTFilesData;


typedef struct IndexDataStringList_ {
  struct IndexDataStringList_ *next;
  char data[1]; /* Dynamically sized. */
} IndexDataStringList;

typedef struct {
  const char *check_prefix;
  int first_check_printed;
  int fail_for_error;
  int abort;
  CXString main_filename;
  ImportedASTFilesData *importedASTs;
  IndexDataStringList *strings;
  CXTranslationUnit TU;
} IndexData;

const char* clang_arguments[MAX_CLANG_ARGUMENTS];
int num_clang_arguments = 0;

static CXIdxClientFile IncludeFile(CXClientData client_data, const CXIdxIncludedFileInfo *info)
{
  IndexData *index_data;
  CXModule Mod;
  index_data = (IndexData *)client_data;

  CXString test = clang_getFileName(info->file);
  const char *csstr = clang_getCString(test);
  CXIndex x = clang_createIndex(1, 1);
  CXTranslationUnit u = clang_createTranslationUnitFromSourceFile(x, csstr, num_clang_arguments, clang_arguments, 0, 0);
  parsePreprocessorDefines(u);
  clang_disposeIndex(x);
  clang_disposeTranslationUnit(u);
  
  return (CXIdxClientFile)info->file;
}

static IndexerCallbacks indexerCallbacks = { 0 };

static CXString createCXString(const char *CS) {
  CXString Str;
  Str.data = (const void *)CS;
  Str.private_flags = 0;
  return Str;
}

int _tmain(int argc, _TCHAR* argv[])
{

  char sourceFile[0x1000];
  const char* outFile = "type_db.bin";

  /* parse arguments */
  if ((argc < 2) || (argc > MAX_CLANG_ARGUMENTS - 5))
  {
    printf("This is type_parser v0.8.1.0\n");
    printf("This program will extract type definitions from the given compilation unit and store them in a *.bin file\n");
    printf("The *.bin file then can be used by backends for, e.g. transcribing into another programming language.\n\n");
    
    printf("Usage: %ls <source_file> [list of arguments directly passsed to clang]\n", argv[0]);

    return -1;
  }

  clang_arguments[num_clang_arguments++] = "-c";
  WideCharToMultiByte(CP_ACP, 0, argv[1], wcslen(argv[1]) + 1, sourceFile, 255, NULL, NULL);

  for (int i = 2; i < argc; i++)
  {
    char *argument = (char*)malloc(sizeof(char) * 255);
    memset(argument, 0, 255);
    WideCharToMultiByte(CP_ACP, 0, argv[i], wcslen(argv[i]), argument, 255, NULL, NULL);
    
    clang_arguments[num_clang_arguments] = argument;
    num_clang_arguments++;
    assert(num_clang_arguments <= MAX_CLANG_ARGUMENTS);
  }

  CXTranslationUnit translationUnit;
  CXIndex index = clang_createIndex(1, 1);


  printf("Trying to compile \"%s\" and extract type database into into \"%s\"\n", sourceFile, outFile);

  if (!index) {
    printf("Couldn't create CXIndex");
    return 0;
  }

  /* run compiler on given translation unit */
  translationUnit = clang_parseTranslationUnit(index,
    sourceFile,
    clang_arguments,
    num_clang_arguments,
    NULL,
    0,
    CXTranslationUnit_None);

  

  int numDiags = clang_getNumDiagnostics(translationUnit);
  for (int i = 0; i < numDiags; i++)
  {
    CXDiagnostic diag = clang_getDiagnostic(translationUnit, i);
    CXDiagnosticSeverity severity = clang_getDiagnosticSeverity(diag);
    if ((severity == CXDiagnostic_Error) || (severity == CXDiagnostic_Fatal)) {
      printf("Error(s) in translation unit. Database creation failed.\n");
      return -1;
    }
  }

  if (!translationUnit) {
    printf("Couldn't create CXTranslationUnit of \"%s\"\n", sourceFile);
    return 0;
  }

  CXTranslationUnit translationUnit2;
  translationUnit2 = clang_createTranslationUnitFromSourceFile(index, sourceFile, num_clang_arguments, clang_arguments, 0, 0);
  clang_disposeIndex(index);

  /* first, lets get the #defines from the preprocessor stage */
  parsePreprocessorDefines(translationUnit);
  index = clang_createIndex(1, 1);
  CXIndexAction action = clang_IndexAction_create(index);
  indexerCallbacks.ppIncludedFile = IncludeFile;

  IndexData index_data;

  index_data.main_filename = createCXString("");
  index_data.strings = NULL;
  index_data.TU = translationUnit2;

  int ret = clang_indexTranslationUnit(action, &index_data, &indexerCallbacks, sizeof(indexerCallbacks), CXIndexOpt_SuppressWarnings, translationUnit2);
  clang_disposeIndex(index);
  clang_disposeTranslationUnit(translationUnit2);
  clang_IndexAction_dispose(action);
  
  /* emit defines */
  for (define_t *ptDefine = (define_t*)queueIterBegin(&defineList); queueIterHasNext(&ptDefine->tElem); ptDefine = (define_t*)queueIterNext(&ptDefine->tElem))
  {
    printf("#define %s %s\n", ptDefine->abIdentifier, ptDefine->abLiteral);
  }

  /* second, get the type tree from the compilation stage */

  /* Parsing ok: Now traverse the AST */
  CXCursor cursor = clang_getTranslationUnitCursor(translationUnit);

  /* "myVisitor" will be called back for all children of the AST */
  clang_visitChildren(cursor, myVisitor, NULL);
  
  /* emit types */
  dump_type_db();

  /* write output file */
  FILE* fout = fopen(outFile, "wb");
  if (fout == NULL)
  {
    printf("Failed to open output file \"%s\"\n", outFile);
    return -1;
  }
  serialize_type_db(fout);
  serialize_define_db(fout);
  fclose(fout);
  printf("Done\n");

  return 0;
}
