/*
 * ropes.c
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
#include "Python.h"
#include "stdlib.h"
#include "math.h"
#include "limits.h"

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

static PyTypeObject ropes_type;
static PyTypeObject rope_iter_type;

unsigned int hash_table[256];
int num_bits;

static PyObject *
python_rope_new()
{
	PyObject *tuple = PyTuple_New(0);
	PyObject *dict = PyDict_New();
	PyRopeObject *new_rope =
		(PyRopeObject *) PyObject_Call((PyObject *)&ropes_type, tuple,
					       dict);
	new_rope->n_type = ROPE_UNINITIALIZED_NODE;
	new_rope->n_hash = -1;
	new_rope->n_length = 0;
	Py_DECREF(tuple);
	Py_DECREF(dict);
	return (PyObject *)new_rope;
}

/* XXX Does this even do anything? */
static void
rope_init_hash()
{
	int curval = 1;
	int i;
	num_bits = (int)((log(LONG_MAX) / log(2)) + 1.0f);
	for (; i < 256; i++) {
		curval = 1 << (i % num_bits);
		curval ^= i << ((i + 4) % num_bits);
		hash_table[i] = curval;
	}
}

static void
rope_copy(PyRopeObject * dest, PyRopeObject * src)
{
	dest->n_type = src->n_type;
	dest->n_length = src->n_length;
	dest->n_hash = src->n_hash;
	switch (dest->n_type) {
	case ROPE_CONCAT_NODE:
		dest->n_node.concat.left = src->n_node.concat.left;
		dest->n_node.concat.right = src->n_node.concat.right;
		Py_XINCREF(dest->n_node.concat.left);
		Py_XINCREF(dest->n_node.concat.right);
		break;
	case ROPE_MULTIPLY_NODE:
		dest->n_node.concat.left = src->n_node.concat.left;
		dest->n_node.concat.right = src->n_node.concat.right;
		Py_XINCREF(dest->n_node.concat.left);
		Py_XINCREF(dest->n_node.concat.right);
		dest->n_node.multiply.m_times = src->n_node.multiply.m_times;
		break;
	case ROPE_SLICE_NODE:
		dest->n_node.concat.left = src->n_node.concat.left;
		dest->n_node.concat.right = src->n_node.concat.right;
		Py_XINCREF(dest->n_node.concat.left);
		Py_XINCREF(dest->n_node.concat.right);
		dest->n_node.slice.start = src->n_node.slice.start;
		dest->n_node.slice.end = src->n_node.slice.end;
		break;
	case ROPE_LITERAL_NODE:
		dest->n_node.literal.l_literal = PyMem_Malloc(src->n_length);
		memcpy(dest->n_node.literal.l_literal,
		       src->n_node.literal.l_literal, src->n_length);
		break;
	}
}

static void
rope_to_string(PyRopeObject * node, char *v, unsigned int w, int offset,
	       int length)
{
	int whole_length;
	if (!node || node->n_length == 0)
		return;
	if (offset > node->n_length)
		return;
	if (offset < 0)
		offset = 0;
	if (length < 0)
		length = 0;
	whole_length = length;
	switch (node->n_type) {
	case ROPE_SLICE_NODE:
		offset = node->n_node.slice.start;
		length = node->n_length;	/* This is supposed to fall through. This avoids repetition and another layer of recursion */
	case ROPE_CONCAT_NODE:
		if (offset <= node->n_node.concat.left->n_length) {
			rope_to_string(node->n_node.concat.left, v, w, offset,
				       length);
			length -=
				(node->n_node.concat.left->n_length - offset);
			w += (node->n_node.concat.left->n_length - offset);
		}
		offset -= (node->n_node.concat.left->n_length);
		rope_to_string(node->n_node.concat.right, v, w, offset,
			       length);
		break;
	case ROPE_MULTIPLY_NODE:
		{
			int base_length =
				node->n_length / node->n_node.multiply.m_times;
			if ((offset + length) < base_length) {
				rope_to_string(node->n_node.concat.left, v, w,
					       offset, length);
				break;
			}
			else {
				int i = 0;
				int times = length / base_length;
				times += (length -
					  (times * base_length) ? 1 : 0);
				offset = offset % base_length;
				length = ((length % base_length) ==
					  0 ? base_length : (length %
							     base_length));
				for (; i < times; i++) {
					if (i == (times - 1) &&
					    node->n_node.multiply.left) {
						rope_to_string(node->n_node.
							       multiply.left,
							       v, w, 0,
							       length + 1);
						w += length + 1;
					}
					else if (i == 0 &&
						 node->n_node.multiply.left) {
						rope_to_string(node->n_node.
							       multiply.left,
							       v, w, offset,
							       node->n_node.
							       multiply.left->
							       n_length -
							       offset);
						w += node->n_node.multiply.
							left->n_length -
							offset;
					}
					else if (node->n_node.multiply.left) {
						rope_to_string(node->n_node.
							       multiply.left,
							       v, w, 0,
							       node->n_node.
							       multiply.left->
							       n_length);
						w += node->n_node.multiply.
							left->n_length;
					}
				}
			}
		}
		break;
	case ROPE_LITERAL_NODE:
		memcpy(v + w, node->n_node.literal.l_literal + offset,
		       (((node->n_length - offset) <
			 length) ? (node->n_length - offset) : length));
		break;
	default:
		break;
	}
}

static char
rope_index(PyRopeObject * r, int i)
{
	volatile PyRopeObject *rope = r;
	volatile int index = i;
	/* TAIL RECURSION!!!!!!! */
      head:
	if (!rope || index > rope->n_length)
		return 0;
	switch (rope->n_type) {
	case ROPE_CONCAT_NODE:
		if (rope->n_node.concat.left) {
			if (index < rope->n_node.concat.left->n_length) {
				rope = rope->n_node.concat.left;
				goto head;
			}
			else {
				index = index -
					rope->n_node.concat.left->n_length;
				rope = rope->n_node.concat.right;
				goto head;
			}
		}
		else if (rope->n_node.concat.right) {
			rope = rope->n_node.concat.right;
			goto head;
		}
		return 0;
	case ROPE_MULTIPLY_NODE:
		{
			int rope_base_length = 0;
			if (rope->n_node.multiply.left)
				rope_base_length +=
					rope->n_node.multiply.left->n_length;
			if (rope->n_node.multiply.right)
				rope_base_length +=
					rope->n_node.multiply.right->n_length;
			index = index % rope_base_length;
			if (rope->n_node.multiply.left) {
				if (index <
				    rope->n_node.multiply.left->n_length) {
					rope = rope->n_node.multiply.left;
					goto head;
				}
				else {
					index = index -
						rope->n_node.multiply.left->
						n_length;
					rope = rope->n_node.multiply.right;
					goto head;
				}
			}
			else if (rope->n_node.concat.right) {
				rope = rope->n_node.concat.right;
				goto head;
			}
		}
		return 0;
	case ROPE_LITERAL_NODE:
		return rope->n_node.literal.l_literal[index];
	default:
		return 0;
	}
}

static void
print_rope(PyRopeObject * node)
{
	if (!node)
		return;
	switch (node->n_type) {
	case ROPE_CONCAT_NODE:
		print_rope(node->n_node.concat.left);
		print_rope(node->n_node.concat.right);
		break;
	case ROPE_MULTIPLY_NODE:
		{
			int i = 0;
			for (; i < node->n_node.multiply.m_times; i++) {
				print_rope(node->n_node.multiply.left);
				print_rope(node->n_node.multiply.right);
			}
		}
		break;
	case ROPE_LITERAL_NODE:
		{
			fwrite(node->n_node.literal.l_literal, node->n_length,
			       1, stdout);
		}
	default:
		break;
	}
	return;
}

static int
rope_get_balance_list_count(PyRopeObject * node)
{
	int retval = 0;
	if (!node)
		return 0;
	if (node->n_type == ROPE_CONCAT_NODE)
		retval +=
			rope_get_balance_list_count(node->n_node.concat.left) +
			rope_get_balance_list_count(node->n_node.concat.right);
	else
		retval = 1;
	return retval;
}

static void
_rope_balance(PyRopeObject ** parent, PyRopeObject ** node_list,
	      int node_list_size)
{
	/* If there is only one node just set it */
	if (node_list_size == 1) {
		*parent = node_list[0];
		return;
	}
	else {
		/* Otherwise we need a concatenation node */
		if (!(*parent)) {
			*parent = (PyRopeObject *) python_rope_new();
			(*parent)->n_type = ROPE_CONCAT_NODE;
		}
		if (node_list_size == 2) {
			/* set the left and rights */
			(*parent)->n_node.concat.left = node_list[0];
			(*parent)->n_node.concat.right = node_list[1];
		}
		else {
			int half_node_list_size = node_list_size / 2;
			int other_half_node_list_size =
				node_list_size - half_node_list_size;
			(*parent)->n_node.concat.left =
				(*parent)->n_node.concat.right = NULL;
			/* Fill in the left and rights */
			_rope_balance(&(*parent)->n_node.concat.left,
				      node_list, half_node_list_size);
			_rope_balance(&(*parent)->n_node.concat.right,
				      node_list + half_node_list_size,
				      other_half_node_list_size);
		}
		(*parent)->n_length =
			(*parent)->n_node.concat.left->n_length +
			((*parent)->n_node.concat.right ? (*parent)->n_node.
			 concat.right->n_length : 0);
	}
}

static int
_rope_get_balance_list(PyRopeObject ** node_list, PyRopeObject * node)
{
	if (!node)
		return 0;
	if (node->n_type == ROPE_CONCAT_NODE) {
		int where = _rope_get_balance_list(node_list,
						   node->n_node.concat.left);
		where += _rope_get_balance_list(node_list + where,
						node->n_node.concat.right);
		return where;
	}
	else {
		*node_list = node;
		return 1;
	}
	return 0;
}

/* This function increments the reference counts of all nodes that are not concatenation nodes. When we delete the concatenation nodes later
   None of the literal or other node types will be deleted because their reference counts are > 1 */
static void
_rope_balance_del(PyRopeObject * node)
{
	if (!node)
		return;
	if (node->n_type == ROPE_CONCAT_NODE) {
		_rope_balance_del(node->n_node.concat.left);
		_rope_balance_del(node->n_node.concat.right);
	}
	else {
		Py_INCREF(node);
	}
	return;
}

static void
rope_balance(PyRopeObject * r)
{
	int blc;
	int i;
	PyRopeObject *old_root, **node_list;
	if (!r || r->n_type != ROPE_CONCAT_NODE)
		return;
	blc = rope_get_balance_list_count(r);
	old_root = r;
	node_list = PyMem_Malloc(sizeof(struct rope_node *) * blc);
	_rope_get_balance_list(node_list, r);
	/* Delete the concatenation nodes */
	_rope_balance_del(r->n_node.concat.left);
	_rope_balance_del(r->n_node.concat.right);
	Py_XDECREF(r->n_node.concat.left);
	Py_XDECREF(r->n_node.concat.right);
	r->n_node.concat.left = r->n_node.concat.right = NULL;
	for (; i < blc; i++) {
		if (node_list[i]->n_type != ROPE_LITERAL_NODE)
			_rope_balance(&node_list[i], node_list, blc);
	}
	/* XXX: Get rebalancing to make sure every node is filled in with the full LITERAL_LENGTH or more bytes of data */
	_rope_balance(&r, node_list, blc);
	PyMem_Free(node_list);
}

static long
rope_hash(PyRopeObject * rope)
{
	long retval = 0;
	if (!rope)
		return 0;
	if (rope->n_type == ROPE_LITERAL_NODE) {
		int i = 0;
		for (; i < rope->n_length; i++) {
			retval ^=
				((hash_table
				  [(unsigned char)rope->n_node.literal.
				   l_literal[i]]));
		}
	}
	else {
		if (rope->n_node.concat.left &&
		    rope->n_node.concat.left->n_hash == -1)
			rope_hash(rope->n_node.concat.left);
		if (rope->n_node.concat.right &&
		    rope->n_node.concat.right->n_hash == -1)
			rope_hash(rope->n_node.concat.right);
		if (rope->n_node.concat.left && rope->n_node.concat.right) {
			retval = rope->n_node.concat.left->n_hash ^ rope->
				n_node.concat.right->n_hash;
		}
		else if (rope->n_node.concat.right)
			retval = rope->n_node.concat.right->n_hash;
		else
			retval = rope->n_node.concat.left->n_hash;
	}
	rope->n_hash = retval;
	return retval;
}

static PyRopeObject *
PyObject_to_rope_node(PyObject *obj)
{
	if (obj->ob_type == &ropes_type) {
		Py_INCREF(obj);
		return (PyRopeObject *) obj;
	}
	else if (PyString_Check(obj) && PyString_Size(obj) < LITERAL_LENGTH) {
		PyRopeObject *ln = (PyRopeObject *) python_rope_new();
		ln->n_type = ROPE_LITERAL_NODE;
		ln->n_length = strlen(PyString_AsString(obj));
		ln->n_node.literal.l_literal =
			PyMem_Malloc(PyString_Size(obj));
		memcpy(ln->n_node.literal.l_literal, PyString_AsString(obj),
		       ln->n_length);
		return ln;
	}
	else if (PyString_Check(obj)) {
		PyRopeObject *retval = (PyRopeObject *) python_rope_new();
		PyRopeObject *old_retval;
		char *c;
		int size, i = 0;
		retval->n_type = ROPE_CONCAT_NODE;
		retval->n_node.concat.left = NULL;
		retval->n_node.concat.right = NULL;
		retval->n_length = 0;
		c = PyString_AsString(obj);
		size = strlen(c);
		for (; i < ((size / LITERAL_LENGTH) + 1); i++) {
			PyRopeObject *ln;
			int cur_size =
				strlen(c) >
				LITERAL_LENGTH ? LITERAL_LENGTH : strlen(c);
			if (cur_size == 0)
				break;
			ln = (PyRopeObject *) python_rope_new();
			ln->n_type = ROPE_LITERAL_NODE;
			ln->n_length = cur_size;
			ln->n_node.literal.l_literal = PyMem_Malloc(cur_size);
			memcpy(ln->n_node.literal.l_literal, c, cur_size);
			if (retval->n_node.concat.left)
				retval->n_node.concat.right = ln;
			else
				retval->n_node.concat.left = ln;
			if (retval->n_node.concat.right) {
				old_retval = retval;
				retval->n_length += ln->n_length;
				retval = (PyRopeObject *) python_rope_new();
				retval->n_type = ROPE_CONCAT_NODE;
				retval->n_node.concat.left = old_retval;
				retval->n_node.concat.right = NULL;
				retval->n_length = old_retval->n_length;
			}
			c += LITERAL_LENGTH;
		}
		rope_balance(retval);
		return retval;
	}
	return NULL;
}

static void
ropeobj_dealloc(PyRopeObject * r)
{
	if (r->n_type != ROPE_LITERAL_NODE) {
/* 	if(r->n_node.concat.left->ob_refcnt==1) */
/* 	  { */

/* 	printf("delete!\n"); */
/* 	print_rope(r->n_node.concat.left); */
/* 	  } */
		Py_XDECREF(r->n_node.concat.left);	/* If it goes to 0, this function will recurse */
/* 		if(r->n_node.concat.right->ob_refcnt==1) */
/* 	  { */

/* 	printf("delete!\n"); */
/* 	print_rope(r->n_node.concat.right); */
/* 	  } */
		Py_XDECREF(r->n_node.concat.right);
	}
	else {
		PyMem_Free(r->n_node.literal.l_literal);
	}
	PyObject_Del((PyObject *)r);
}

static void
ropeiter_dealloc(PyRopeIterObject *r)
{
	if (r->rope->n_type == ROPE_MULTIPLY_NODE) {
		int i = 0;
		int base_length =
			(r->rope->n_length / r->rope->n_node.multiply.m_times);
		for (; i < base_length; i++)
			Py_XDECREF(r->cached[i]);
		PyMem_Free(r->cached);
	}
	Py_DECREF(r->rope);
	PyObject_Del(r);
}

static PyObject *
ropeobj_new(PyTypeObject * type, PyObject *args, PyObject *kwds)
{
	PyRopeObject *new;
	new = (PyRopeObject *) type->tp_alloc(type, 0);
	new->n_type = ROPE_UNINITIALIZED_NODE;
	new->n_length = 0;
	return (PyObject *)new;
}

static int
ropeobj_initobj(PyRopeObject * self, PyObject *args, PyObject *kwds)
{
	if (self->n_type != ROPE_LITERAL_NODE) {
		Py_XDECREF(self->n_node.concat.left);
		Py_XDECREF(self->n_node.concat.right);
	}
	if (PyTuple_Size(args) == 1) {
		rope_copy(self,
			  PyObject_to_rope_node(PyTuple_GetItem(args, 0)));
	}
	else if (PyTuple_Size(args) > 1) {
		int i = 0;
		PyRopeObject *old_rope;
		PyRopeObject *rope = (PyRopeObject *) python_rope_new();
		rope->n_type = ROPE_CONCAT_NODE;
		rope->n_node.concat.left = self->n_node.concat.right = NULL;
		rope->n_length = 0;
		for (; i < PyTuple_Size(args); i++) {
			PyRopeObject *cur =
				PyObject_to_rope_node(PyTuple_GetItem
						      (args, i));
			if (rope->n_node.concat.left)
				rope->n_node.concat.right = cur;
			else
				rope->n_node.concat.left = cur;
			rope->n_length += cur->n_length;
			if (rope->n_node.concat.right) {
				old_rope = rope;
				rope = (PyRopeObject *) python_rope_new();
				rope->n_type = ROPE_CONCAT_NODE;
				rope->n_length = old_rope->n_length;
				rope->n_node.concat.left = old_rope;
				rope->n_node.concat.right = NULL;
			}
		}
		rope_copy(self, rope);
		Py_DECREF(rope);
	}
	return 0;
}

static PyObject *
ropeobj_str(PyRopeObject * self)
{
	PyObject *retval;
	char *v = PyMem_Malloc(self->n_length);
	rope_to_string(self, v, 0, 0, self->n_length);
	retval = PyString_FromStringAndSize(v, self->n_length);
	PyMem_Free(v);
	return retval;
}

static PyObject *
ropeobj_repr(PyRopeObject * r)
{
	PyObject *retval;
	PyObject *repr;
	char *v = PyMem_Malloc(r->n_length);
	char *retval_str;
	rope_to_string(r, v, 0, 0, r->n_length);
	retval = PyString_FromStringAndSize(v, r->n_length);
	repr = PyString_Repr(retval, 1);
	retval_str = PyMem_Malloc(PyObject_Length(repr) + strlen("Rope()"));
	Py_DECREF(retval);
	memcpy(retval_str, "Rope(", strlen("Rope("));
	memcpy(retval_str + strlen("Rope("), PyString_AsString(repr),
	       PyObject_Length(repr));
	memcpy(retval_str + strlen("Rope(") + PyObject_Length(repr), ")", 2);
	retval = PyString_FromStringAndSize(retval_str,
					    PyObject_Length(repr) +
					    strlen("Rope()"));
	PyMem_Free(retval_str);
	PyMem_Free(v);
	Py_DECREF(repr);
	return retval;
}

static PyObject *
ropeobj_concat_new(PyObject *self_obj, PyRopeObject * node)
{
	PyRopeObject *self, *new_rope;
	if (node->ob_type != &ropes_type) {
		PyErr_SetString(PyExc_TypeError,
				"Cannot concatenate a rope with anything other than a rope!");
		return NULL;
	}
	self = (PyRopeObject *) self_obj;
	new_rope = (PyRopeObject *) python_rope_new();
	if (!node) {
		PyErr_SetString(PyExc_TypeError,
				"Cannot concatenate rope with anything other than a string or a rope!");
		return NULL;
	}
	new_rope->n_type = ROPE_CONCAT_NODE;
	new_rope->n_length = self->n_length + node->n_length;
	Py_INCREF(self);
	Py_INCREF(node);
	new_rope->n_node.concat.left = self;
	new_rope->n_node.concat.right = node;
	return (PyObject *)new_rope;
}

static PyObject *
ropeobj_repeat_new(PyObject *self_obj, int by)
{
	PyRopeObject *self, *new_rope;
	if (by < 0) {
		PyErr_SetString(PyExc_TypeError,
				"Can't repeat by a negative number");
		return NULL;
	}
	if (self_obj->ob_type != &ropes_type) {
		PyErr_BadInternalCall();
		return NULL;
	}
	self = (PyRopeObject *) self_obj;
	if (self->n_type == ROPE_UNINITIALIZED_NODE || self->n_length == 0) {
		PyErr_SetString(PyExc_ValueError, "Can't repeat empty rope");
	}
	Py_INCREF(self);
	new_rope = (PyRopeObject *) python_rope_new();
	new_rope->n_type = ROPE_MULTIPLY_NODE;
	new_rope->n_length = self->n_length * by;
	new_rope->n_node.multiply.m_times = by;
	new_rope->n_node.multiply.right = NULL;
	new_rope->n_node.multiply.left = self;
	return (PyObject *)new_rope;
}

static PyObject *
ropeobj_blc(PyRopeObject * self, PyObject *args, PyObject *kwds)
{
	rope_balance(self);
	Py_RETURN_NONE;
}

static int
ropeobj_length(PyRopeObject * r)
{
	return r->n_length;
}

/* static PyObject* */
/* ropeobj_get_type(PyRopeObject* self) */
/* { */
/*   return PyInt_FromLong(self->n_type); */
/* } */

/* static PyObject* */
/* ropeobj_get_left(PyRopeObject* self) */
/* { */
/*   if(self->n_type!=ROPE_CONCAT_NODE && self->n_type!=ROPE_MULTIPLY_NODE) */
/* 	Py_RETURN_NONE; */
/*   Py_INCREF(self->n_node.concat.left); */
/*   return (PyObject*) self->n_node.concat.left; */
/* } */

/* static PyObject* */
/* ropeobj_get_right(PyRopeObject* self) */
/* { */
/*   if(self->n_type!=ROPE_CONCAT_NODE && self->n_type!=ROPE_MULTIPLY_NODE) */
/* 	Py_RETURN_NONE; */
/*   Py_INCREF(self->n_node.concat.right); */
/*   return (PyObject*) self->n_node.concat.right; */
/* } */

/* static PyObject* */
/* ropeobj_get_times(PyRopeObject* self) */
/* { */
/*   if(self->n_type!=ROPE_MULTIPLY_NODE) */
/* 	Py_RETURN_NONE; */
/*   return PyInt_FromLong(self->n_node.multiply.m_times); */
/* } */

/* static PyObject* */
/* ropeobj_get_literal(PyRopeObject* self) */
/* { */
/*   if(self->n_type!=ROPE_LITERAL_NODE) */
/* 	Py_RETURN_NONE; */
/*   return PyString_FromStringAndSize(self->n_node.literal.l_literal,self->n_length); */
/* } */

static PyObject *
ropeobj_item(PyRopeObject * self, int i)
{
	char c;
	if (i >= self->n_length) {
		PyErr_SetString(PyExc_IndexError, "rope index out of range");
		return NULL;
	}
	c = rope_index(self, i);
	return PyString_FromStringAndSize(&c, 1);
}

static PyObject *
ropeobj_slice(PyRopeObject * self, int start, int end)
{
	PyRopeObject *rope;
	if (end > self->n_length)
		end = self->n_length;
	if (start > self->n_length)
		start = self->n_length;
	rope = (PyRopeObject *) python_rope_new();
	if (rope == NULL)
		return PyErr_NoMemory();
	rope->n_type = ROPE_SLICE_NODE;
	rope->n_length = end - start;
	rope->n_node.slice.left = self;
	rope->n_node.slice.right = NULL;
	rope->n_node.slice.start = start;
	rope->n_node.slice.end = end;
	return (PyObject *)rope;
}

static int
ropeobj_compare(PyRopeObject * self, PyRopeObject * other)
{
	int retval = -1;
	int i = 0;
	PyRopeIterObject *self_iter, *other_iter;
	PyObject *self_cur, *other_cur;
	if (self->n_length != other->n_length) {
		if (self->n_length < other->n_length)
			return -1;
		else
			return 1;
	}
	self_iter = (PyRopeIterObject *)PyObject_GetIter((PyObject *)self);
	if (!self_iter)
		return -1;
	other_iter = (PyRopeIterObject *)PyObject_GetIter((PyObject *)other);
	if (!other_iter)
		return -1;
	for (; i < self->n_length; i++) {
		self_cur = PyIter_Next((PyObject *)self_iter);
		if (!self_cur)
			return -1;
		other_cur = PyIter_Next((PyObject *)other_iter);
		if (!other_cur)
			return -1;
		retval = PyObject_Compare(self_cur, other_cur);
		if (retval != 0) {
			Py_DECREF(self_cur);
			Py_DECREF(other_cur);
			return retval;
		}
		Py_DECREF(self_cur);
		Py_DECREF(other_cur);
	}
	Py_DECREF(self_iter);
	Py_DECREF(other_iter);
	return retval;
}

static PyObject *
ropeiter_next(PyRopeIterObject *self)
{
	if (self->pos >= self->rope->n_length &&
	    self->rope->n_type != ROPE_MULTIPLY_NODE)
		return NULL;
	switch (self->rope->n_type) {
	case ROPE_LITERAL_NODE:
		return PyString_FromStringAndSize(self->rope->n_node.literal.
						  l_literal + (self->pos++),
						  1);
		break;
	case ROPE_SLICE_NODE:
		self->pos++;
		if ((self->pos + self->rope->n_node.slice.start) >
		    self->rope->n_node.slice.end)
			return NULL;
		return PyIter_Next(self->cur_iter);
		break;
	case ROPE_MULTIPLY_NODE:
		{
			int base_length =
				self->rope->n_length /
				self->rope->n_node.multiply.m_times;
			if (self->pos >= base_length) {
				if (self->times >=
				    self->rope->n_node.multiply.m_times)
					return NULL;
				self->times++;
				self->pos = 0;
			}
			if (self->times == 0) {
				self->cached[self->pos] =
					PyIter_Next(self->cur_iter);
			}
			Py_XINCREF(self->cached[self->pos]);
			return self->cached[(self->pos++)];
		}
		break;
	case ROPE_CONCAT_NODE:
		{
			PyRopeObject *retval =
				(PyRopeObject *) PyIter_Next(self->cur_iter);
			if (!retval && self->is_left) {
				self->is_left = 0;
				self->cur_iter =
					PyObject_GetIter((PyObject *)self->
							 rope->n_node.concat.
							 right);
				if (!self->cur_iter)
					return NULL;
				else
					return PyIter_Next(self->cur_iter);
			}
			return (PyObject *)retval;
		}
		break;
	}
	return NULL;
}

static PyRopeIterObject *
ropeobj_iter(PyRopeObject * self)
{
	PyRopeIterObject *retval =
		(PyRopeIterObject *)PyType_GenericNew(&rope_iter_type, NULL,
						      NULL);
	if (!retval)
		return NULL;
	if (self->n_type == ROPE_UNINITIALIZED_NODE)
		return NULL;
	Py_INCREF(self);
	retval->rope = self;
	retval->is_left = 1;
	retval->pos = 0;
	retval->times = 0;
	if (self->n_type == ROPE_CONCAT_NODE)
		retval->cur_iter =
			PyObject_GetIter((PyObject *)self->n_node.concat.left);
	else if (self->n_type == ROPE_MULTIPLY_NODE) {
		retval->cur_iter =
			PyObject_GetIter((PyObject *)self->n_node.multiply.
					 left);
		retval->cached =
			PyMem_Malloc(sizeof(PyObject *) *
				     (self->n_length /
				      self->n_node.multiply.m_times));
	}
	else if (self->n_type == ROPE_SLICE_NODE) {
		int i = 0;
		retval->cur_iter =
			PyObject_GetIter((PyObject *)self->n_node.slice.left);
		for (; i < self->n_node.slice.start; i++)
			PyIter_Next((PyObject *)retval->cur_iter);
	}
	return retval;
}

static int
ropeobj_contains(PyRopeObject * self, PyRopeObject * other)
{
	PyRopeIterObject *self_iter, *other_iter;
	PyObject *self_cur, *other_cur;
	int i, j;
	i = j = 0;
	if (other->ob_type != &ropes_type) {
		PyErr_SetString(PyExc_TypeError,
				"'in <rope>' requires rope as left operand");
		return -1;
	}
	self_iter = (PyRopeIterObject *)PyObject_GetIter((PyObject *)self);
	if (!self_iter)
		return -1;
	other_iter = (PyRopeIterObject *)PyObject_GetIter((PyObject *)other);
	if (!other_iter)
		return -1;
	other_cur = PyIter_Next((PyObject *)other_iter);
	for (; i < self->n_length; i++) {
		self_cur = PyIter_Next((PyObject *)self_iter);
		if (PyObject_Compare(self_cur, other_cur) != 0) {
			Py_XDECREF(self_cur);
			continue;
		}
		for (; j < (other->n_length - 1); j++) {
			other_cur = PyIter_Next((PyObject *)other_iter);
			self_cur = PyIter_Next((PyObject *)self_iter);
			if (!self_cur || !other_cur) {
				Py_XDECREF(other_cur);
				Py_XDECREF(self_cur);
				Py_XDECREF(other_iter);
				Py_XDECREF(self_iter);
				return -1;
			}
			if (PyObject_Compare(self_cur, other_cur) != 0)
				goto reset;
			Py_XDECREF(other_cur);
			Py_XDECREF(self_cur);
		}
		Py_XDECREF(other_cur);
		Py_XDECREF(self_cur);
		Py_XDECREF(other_iter);
		Py_XDECREF(self_iter);
		return 1;
	      reset:
		Py_XDECREF(self_cur);
		Py_XDECREF(other_cur);
		Py_XDECREF(other_iter);
		other_iter = (PyRopeIterObject *)PyObject_GetIter((PyObject *)
								  other);
		other_cur = PyIter_Next((PyObject *)other_iter);
	}
	Py_XDECREF(other_cur);
	Py_XDECREF(self_cur);
	Py_XDECREF(other_iter);
	Py_XDECREF(self_iter);
	return 0;
}

static PyMethodDef ropes_methods[] = {
	{NULL, NULL, 0, NULL}
};

static PyMethodDef ropeobj_methods[] = {
	{"balance", (PyCFunction) ropeobj_blc, METH_VARARGS, "internal"},
	{NULL, NULL, 0, NULL}
};

static PySequenceMethods ropeobj_as_sequence = {
	(inquiry) ropeobj_length,	/* sq_length */
	(binaryfunc) ropeobj_concat_new,	/* sq_concat */
	(intargfunc) ropeobj_repeat_new,	/* sq_repeat */
	(intargfunc) ropeobj_item,	/* sq_item */
	(intintargfunc) ropeobj_slice,	/* sq_slice */
	NULL,			/* sq_ass_item */
	NULL,			/* sq_ass_slice */
	(objobjproc) ropeobj_contains,	/* sq_contains */
	/* Ropes are meant to be immutable */
	NULL,			/* sq_inplace_concat */
	NULL,			/* sq_inplace_repeat */
};

/* static PyGetSetDef ropeobj_getset[] = */
/*   { */
/* 	{"type", (getter)ropeobj_get_type, NULL, "Get node type", NULL}, */
/* 	{"left", (getter)ropeobj_get_left, NULL, "Get the node to the left", NULL}, */
/* 	{"right", (getter)ropeobj_get_right, NULL, "Get the node to the right", NULL}, */
/* 	{"times", (getter)ropeobj_get_times, NULL, "Get the amount of repetition", NULL}, */
/* 	{"literal", (getter)ropeobj_get_literal, NULL, "Get the literal string", NULL} */
/*   }; */

static PyTypeObject rope_iter_type = {
	PyObject_HEAD_INIT(0)	/* Must fill in type value later */
		0,		/* ob_size */
	"ropes.RopeIter",	/* tp_name */
	sizeof(PyRopeIterObject),	/* tp_basicsize */
	0,			/* tp_itemsize */
	(destructor) ropeiter_dealloc,	/* tp_dealloc */
	0,			/* tp_print */
	0,			/* tp_getattr */
	0,			/* tp_setattr */
	0,			/* tp_compare */
	0,			/* tp_repr */
	0,			/* tp_as_number */
	0,			/* tp_as_sequence */
	0,			/* tp_as_mapping */
	0,			/* tp_hash */
	0,			/* tp_call */
	0,			/* tp_str */
	0,			/* tp_getattro */
	0,			/* tp_setattro */
	0,			/* tp_as_buffer */
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,	/* tp_flags */
	"A Rope Iter type",	/* tp_doc */
	0,			/* tp_traverse */
	0,			/* tp_clear */
	0,			/* tp_richcompare */
	0,			/* tp_weaklistoffset */
	(getiterfunc) PyObject_SelfIter,	/* tp_iter */
	(iternextfunc) ropeiter_next,	/* tp_iternext */
	0,			/* tp_methods */
	0,			/* tp_members */
	0,			/* tp_getset */
	0,			/* tp_base */
	0,			/* tp_dict */
	0,			/* tp_descr_get */
	0,			/* tp_descr_set */
	0,			/* tp_dictoffset */
	0,			/* tp_init */
	PyType_GenericAlloc,	/* tp_alloc */
	0,			/* tp_new */
};

static PyTypeObject ropes_type = {
	PyObject_HEAD_INIT(0)	/* Must fill in type value later */
		0,		/* ob_size */
	"ropes.Rope",		/* tp_name */
	sizeof(PyRopeObject),	/* tp_basicsize */
	0,			/* tp_itemsize */
	(destructor) ropeobj_dealloc,	/* tp_dealloc */
	0,			/* tp_print */
	0,			/* tp_getattr */
	0,			/* tp_setattr */
	(cmpfunc) ropeobj_compare,	/* tp_compare */
	(reprfunc) ropeobj_repr,	/* tp_repr */
	0,			/* tp_as_number */
	&ropeobj_as_sequence,	/* tp_as_sequence */
	0,			/* tp_as_mapping */
	(hashfunc) rope_hash,	/* tp_hash */
	0,			/* tp_call */
	(reprfunc) ropeobj_str,	/* tp_str */
	PyObject_GenericGetAttr,	/* tp_getattro */
	0,			/* tp_setattro */
	0,			/* tp_as_buffer */
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,	/* tp_flags */
	"A Rope type",		/* tp_doc */
	0,			/* tp_traverse */
	0,			/* tp_clear */
	0,			/* tp_richcompare */
	0,			/* tp_weaklistoffset */
	(getiterfunc) ropeobj_iter,	/* tp_iter */
	0,			/* tp_iternext */
	ropeobj_methods,	/* tp_methods */
	0,			/* tp_members */
	0,			/* tp_getset */
	0,			/* tp_base */
	0,			/* tp_dict */
	0,			/* tp_descr_get */
	0,			/* tp_descr_set */
	0,			/* tp_dictoffset */
	(initproc) ropeobj_initobj,	/* tp_init */
	PyType_GenericAlloc,	/* tp_alloc */
	ropeobj_new,		/* tp_new */
	PyObject_Del,		/* tp_free */
};

PyMODINIT_FUNC
initropes()
{
	PyObject *ropes_module;

	rope_init_hash();

	ropes_type.ob_type = &PyType_Type;

	ropes_module =
		Py_InitModule3("ropes", ropes_methods,
			       "Ropes implementation for CPython");
	PyModule_AddIntConstant(ropes_module, "ROPE_UNINITIALIZED_NODE",
				ROPE_UNINITIALIZED_NODE);
	PyModule_AddIntConstant(ropes_module, "ROPE_CONCAT_NODE",
				ROPE_CONCAT_NODE);
	PyModule_AddIntConstant(ropes_module, "ROPE_MULTIPLY_NODE",
				ROPE_MULTIPLY_NODE);
	PyModule_AddIntConstant(ropes_module, "ROPE_LITERAL_NODE",
				ROPE_LITERAL_NODE);
	PyModule_AddIntConstant(ropes_module, "ROPE_SLICE_NODE",
				ROPE_SLICE_NODE);
	Py_INCREF((PyObject *)&ropes_type);
	if (PyModule_AddObject(ropes_module, "Rope", (PyObject *)&ropes_type)
	    != 0)
		return;
}

