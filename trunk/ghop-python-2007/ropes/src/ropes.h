/*
 * ropes.h
 * This file is part of CRopes: A Ropes data type for CPython
 *
 * Copyright (C) 2007 - Travis Athougies
 *
 * CRopes: A Ropes data type for CPython is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by 
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * CRopes: A Ropes data type for CPython is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with CRopes: A Ropes data type for CPython; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, 
 * Boston, MA  02110-1301  USA
 */
#ifndef __ropes_H__
#define __ropes_H__
#include "Python.h"

#define LITERAL_LENGTH 64
#define ROPE_MAX_DEPTH 32

#define ROPE_UNINITIALIZED_NODE 0x0
#define ROPE_CONCAT_NODE 0x1
#define ROPE_MULTIPLY_NODE 0x2
#define ROPE_LITERAL_NODE 0x3
#define ROPE_SLICE_NODE 0x4

typedef struct _PyRopeObject
{
  PyObject_HEAD
  int n_type;
  int n_length;
  long n_hash;
  union
  {
	struct rope_concat_node
	{
	  struct _PyRopeObject *left, *right;
	} concat;

	struct rope_multiply_node
	{
	  struct _PyRopeObject *left, *right;
	  int m_times;
	} multiply;

	struct rope_slice_node
	{
	  struct _PyRopeObject *left, *right;
	  size_t start, end;
	} slice;

	struct rope_literal_node
	{
	  char* l_literal;
	} literal;
  } n_node;
} PyRopeObject;

typedef struct _PyRopeIterObject
{
  PyObject_HEAD
  PyRopeObject* rope;
  int pos;
  PyObject* cur_iter;
  int times;
  PyObject** cached;
  int is_left;
} PyRopeIterObject;

typedef void(*rope_traverse_func)(PyRopeObject*);

PyObject* python_rope_new();
void rope_destroy(PyRopeObject* r);
int rope_append(PyRopeObject* r, PyRopeObject* node);
void rope_to_string(PyRopeObject* node, char* v, unsigned int w, int offset, int length);
void print_rope(PyRopeObject* node);
void rope_balance(PyRopeObject* node);
void rope_incref(PyRopeObject* node);
void rope_copy(PyRopeObject* dest, PyRopeObject* src);
long rope_hash(PyRopeObject* node);

#endif
