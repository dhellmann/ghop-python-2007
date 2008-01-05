/*
 * ropes.c
 * This file is part of CRopes: A Ropes data type for CPython
 *
 * Copyright (C) 2007 - Travis Athougies
 *
 * CRopes: A Ropes data type for CPython is free software; you can
 * redistribute it and/or modify it under the terms of the GNU General
 * Public License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * CRopes: A Ropes data type for CPython is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the
 * implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 * PURPOSE.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with CRopes: A Ropes data type for CPython; if not, write to
 * the Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301 USA
 */

#include "Python.h"
#include "stdlib.h"
#include "math.h"
#include "limits.h"

/* XXX More documentation */
PyDoc_STRVAR(ropes_module_doc,
"Ropes implementation for CPython");


#define DEBUG 0

#define ROPE_MAX_DEPTH 32

/**** Start of reviewed part *****************************************/

enum node_type {
	LITERAL_NODE,
	CONCAT_NODE,
	REPEAT_NODE,
        SLICE_NODE              /* unsupported */
};

typedef struct RopeObject {
	PyObject_HEAD
	enum node_type type;
	Py_ssize_t length;
	long hash;		/* -1 if not computed. */
	int depth;		/* 0 if not a concat. node */
	union {
		char *literal;
		struct concat_node {
			struct RopeObject *left;
			struct RopeObject *right;
		} concat;
		struct repeat_node {
			struct RopeObject *child;
			int count;
		} repeat;
		struct slice_node {
			struct RopeObject *child;
			Py_ssize_t start;
		} slice;
	} v;
} RopeObject;

static PyTypeObject Rope_Type;
static PyTypeObject RopeIter_Type;

static void rope_balance(RopeObject* r);

#define Rope_Check(op) (((PyObject *)(op))->ob_type == &Rope_Type)

/* For debugging only. */
static void
print_rope(RopeObject *self)
{
	int i;

	switch (self->type) {
	case CONCAT_NODE:
		print_rope(self->v.concat.left);
		print_rope(self->v.concat.right);
		break;
	case REPEAT_NODE:
		for (i = 0; i < self->v.repeat.count; i++) {
			print_rope(self->v.repeat.child);
		}
		break;
	case LITERAL_NODE:
		fwrite(self->v.literal, 1, self->length, stdout);
	default:
		break;
	}
}

static void
_rope_str(RopeObject *rope, char **p)
{
	int i;
	char *q;
	char *tmp;

	switch (rope->type) {
	case LITERAL_NODE:
		memcpy(*p, rope->v.literal, rope->length);
		*p += rope->length;
		break;
	case CONCAT_NODE:
		if (rope->v.concat.left)
			_rope_str(rope->v.concat.left, p);
		if (rope->v.concat.right)
			_rope_str(rope->v.concat.right, p);
		break;
	case REPEAT_NODE:
		for (i = 0; i < rope->v.repeat.count; i++)
			_rope_str(rope->v.repeat.child, p);

#if 0
		q = (char *)PyMem_Malloc(rope->length * sizeof(char));
		if (q == NULL) {
			PyErr_NoMemory();
			return;
		}
		tmp = q;
		_rope_str(rope->v.repeat.child, &tmp);
		for (i = 0; i < rope->v.repeat.count; i++) {
			memcpy(*p, q, rope->v.repeat.child->length);
			p += rope->v.repeat.child->length;
		}
		PyMem_Free(q);
#endif
	}
}

static PyObject *
rope_str(RopeObject *self)
{
	PyObject *str;
	char *p;

	str = PyString_FromStringAndSize(NULL, self->length);
	if (str == NULL)
		return NULL;
	p = PyString_AS_STRING(str);
	_rope_str(self, &p);
	if (PyErr_Occurred())
		return NULL;

	return str;
}

static PyObject *
rope_repr(RopeObject *self)
{
	PyObject *v;
	PyObject *str;
	PyObject *repr;
	Py_ssize_t len;
	const char *left_quote = "Rope(";
	char *p;

	str = rope_str(self);
	if (str == NULL)
		return NULL;

	repr = PyString_Repr(str, 1);
	Py_DECREF(str);
	if (repr == NULL)
		return NULL;

	len = PyString_GET_SIZE(repr) + strlen(left_quote) + 2;
	v = PyString_FromStringAndSize(NULL, len);
	if (v == NULL)
		return NULL;

	p = PyString_AS_STRING(v);
	while (*left_quote)
		*p++ = *left_quote++;
	memcpy(p, PyString_AS_STRING(repr), PyString_GET_SIZE(repr));
	p += PyString_GET_SIZE(repr);
	*p++ = ')';
	*p = '\0';

	Py_DECREF(repr);
	if (_PyString_Resize(&v, (p - PyString_AS_STRING(v)))) {
		Py_DECREF(v);
		return NULL;
	}
	return v;
}

static char
rope_index(RopeObject *self, Py_ssize_t i)
{
	assert (self && i < rope->length);

	switch (self->type) {
	case LITERAL_NODE:
		return self->v.literal[i];
	case CONCAT_NODE:
		if (self->v.concat.left && i < self->v.concat.left->length) {
			return rope_index(self->v.concat.left, i);
		}
		else if (self->v.concat.right) {
			i = i - self->v.concat.right->length;
			return rope_index(self->v.concat.right, i);
		}
		break;
	case REPEAT_NODE:
		if (self->v.repeat.child) {
			return rope_index(self->v.repeat.child,
					  i % self->v.repeat.child->length);
		}
		break;
	}

	/* never reached */
	return -1;
}

static PyObject *
rope_getitem(RopeObject *self, Py_ssize_t i)
{
	char c;
	if (i >= self->length) {
		PyErr_SetString(PyExc_IndexError, "rope index out of range");
		return NULL;
	}
	c = rope_index(self, i);
	return PyString_FromStringAndSize(&c, 1);
}

static void
rope_dealloc(RopeObject *self)
{
	switch (self->type) {
	case LITERAL_NODE:
		PyMem_Free(self->v.literal);
		break;
	case CONCAT_NODE:
		Py_XDECREF(self->v.concat.left);
		Py_XDECREF(self->v.concat.right);
		break;
	case REPEAT_NODE:
		Py_XDECREF(self->v.repeat.child);
		break;
	case SLICE_NODE:
		Py_XDECREF(self->v.slice.child);
		break;
	}
	((PyObject *)self)->ob_type->tp_free(self);
}

static RopeObject *
rope_from_type(enum node_type type, Py_ssize_t len)
{
	RopeObject *new;

	assert(len > 0);
	new = PyObject_New(RopeObject, &Rope_Type);
	if (new == NULL)
		return NULL;

	new->type = type;
	new->length = len;
	new->hash = -1;
	return new;
}

static PyObject *
rope_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
	RopeObject *self;

	PyObject *str = NULL;
	const char *literal;
	Py_ssize_t length;

	if (!PyArg_ParseTuple(args, "|O:Rope", &str))
		return NULL;

	if (str == NULL || str == Py_None)
		str = PyString_FromString("");
	if (!PyString_Check(str)) {
		PyErr_Format(PyExc_TypeError,
			     "expected string argument, not %.50s",
			     str->ob_type->tp_name);
		return NULL;
	}
	literal = PyString_AS_STRING(str);
	length = PyString_GET_SIZE(str);

	self = rope_from_type(LITERAL_NODE, length);
	self->v.literal = (char *)PyMem_Malloc(length * sizeof(char));
	strcpy(self->v.literal, literal);

	return (PyObject *)self;
}

static RopeObject *
rope_concat(RopeObject *self, RopeObject *other)
{
	RopeObject *result;

	if (!Rope_Check(other)) {
		PyErr_Format(PyExc_TypeError,
			     "cannot concatenate Rope with '%.50s'",
			     ((PyObject *)other)->ob_type->tp_name);
		return NULL;
	}

	Py_INCREF(self);
	Py_INCREF(other);
	result = rope_from_type(CONCAT_NODE, self->length + other->length);
	result->v.concat.left = self;
	result->v.concat.right = other;

	return result;
}

static RopeObject *
rope_repeat(RopeObject *self, int count)
{
	RopeObject *result;

	if (count <= 1) {
		Py_INCREF(self);
		return self;
	}
	result = rope_from_type(REPEAT_NODE, self->length * count);
	result->v.repeat.count = count;
	Py_INCREF(self);
	result->v.repeat.child = self;	

	return result;
}

typedef int (*traverse_func)(char c, void *arg);

static int
rope_traverse(RopeObject *self, traverse_func f, void *arg)
{
	int status = -1;
	Py_ssize_t i;

	switch (self->type) {
	case LITERAL_NODE:
		for (i = 0; i < self->length; i++) {
			status = (*f)(self->v.literal[i], arg);
			if (status == -1)
				return -1;
		}
		break;
	case CONCAT_NODE:
		if (self->v.concat.left)
			status = rope_traverse(self->v.concat.left, f, arg);
		if (self->v.concat.right)
			status = rope_traverse(self->v.concat.right, f, arg);
		break;
	case REPEAT_NODE:
		for (i = 0; i < self->v.repeat.count; i++)
			status = rope_traverse(self->v.repeat.child, f, arg);
		break;
	}

	return status;
}

static int
_rope_hash(char c, long *x)
{
	*x = (1000003 * (*x)) ^ c;
	return 0;
}

static long
rope_hash(RopeObject *self)
{
	long hash;
	long *p;

	if (self->hash != -1)
		return self->hash;
	hash = rope_index(self, 0) << 7;
	p = &hash;
	rope_traverse(self, (traverse_func)_rope_hash, p);
	hash = *p;
	hash ^= self->length;
	if (hash == -1)
		hash = -2;
	self->hash = hash;
	return hash;
}

static Py_ssize_t
rope_length(RopeObject *self)
{
	return self->length;
}

/**** End of reviewed part *******************************************/

static int
rope_get_balance_list_count(RopeObject *node)
{
	int retval = 0;
	if (!node)
		return 0;
	if (node->type == CONCAT_NODE)
		retval +=
			rope_get_balance_list_count(node->v.concat.left) +
			rope_get_balance_list_count(node->v.concat.right);
	else
		retval = 1;
	return retval;
}

static void
_rope_balance(RopeObject **parent, RopeObject **node_list,
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
		  *parent = (RopeObject *)rope_from_type(CONCAT_NODE, 0);
		}
		if (node_list_size == 2) {
			/* set the left and rights */
			(*parent)->v.concat.left = node_list[0];
			(*parent)->v.concat.right = node_list[1];
		}
		else {
			int half_node_list_size = node_list_size / 2;
			int other_half_node_list_size =
				node_list_size - half_node_list_size;
			(*parent)->v.concat.left =
				(*parent)->v.concat.right = NULL;
			/* Fill in the left and rights */
			_rope_balance(&(*parent)->v.concat.left,
				      node_list, half_node_list_size);
			_rope_balance(&(*parent)->v.concat.right,
				      node_list + half_node_list_size,
				      other_half_node_list_size);
		}
		(*parent)->length =
			(*parent)->v.concat.left->length +
			((*parent)->v.concat.right ? (*parent)->v.
			 concat.right->length : 0);
	}
}

static int
_rope_get_balance_list(RopeObject **node_list, RopeObject *node)
{
	if (!node)
		return 0;
	if (node->type == CONCAT_NODE) {
		int where = _rope_get_balance_list(node_list,
						   node->v.concat.left);
		where += _rope_get_balance_list(node_list + where,
						node->v.concat.right);
		return where;
	}
	else {
		*node_list = node;
		return 1;
	}
	return 0;
}

/* This function increments the reference counts of all nodes that are not
   concatenation nodes. When we delete the concatenation nodes later None of
   the literal or other node types will be deleted because their reference
   counts are > 1 */
static void
_rope_balance_del(RopeObject *node)
{
	if (!node)
		return;
	if (node->type == CONCAT_NODE) {
		_rope_balance_del(node->v.concat.left);
		_rope_balance_del(node->v.concat.right);
	}
	else {
		Py_INCREF(node);
	}
	return;
}

static void
rope_balance(RopeObject *r)
{
	int blc;
	int i;
	RopeObject *old_root, **node_list;
	if (!r || r->type != CONCAT_NODE)
		return;
	blc = rope_get_balance_list_count(r);
	old_root = r;
	node_list = PyMem_Malloc(sizeof(struct rope_node *) * blc);
	_rope_get_balance_list(node_list, r);
	/* Delete the concatenation nodes */
	_rope_balance_del(r->v.concat.left);
	_rope_balance_del(r->v.concat.right);
	Py_XDECREF(r->v.concat.left);
	Py_XDECREF(r->v.concat.right);
	r->v.concat.left = r->v.concat.right = NULL;
	for (; i < blc; i++) {
		if (node_list[i]->type != LITERAL_NODE)
			_rope_balance(&node_list[i], node_list, blc);
	}
	/* XXX: Get rebalancing to make sure every node is filled in with the
	   full LITERAL_LENGTH or more bytes of data */
	_rope_balance(&r, node_list, blc);
	PyMem_Free(node_list);
}

typedef struct RopeIter {
  PyObject_HEAD
  RopeObject* rope;
  RopeObject** list;
  Py_ssize_t list_length;
  char* cur;
  Py_ssize_t cur_length;
  Py_ssize_t base_length;
  Py_ssize_t pos, list_pos, cur_pos;
} RopeIter;

static void
ropeiter_dealloc(RopeIter * r)
{
	PyMem_Free(r->list);
	Py_DECREF(r->rope);
	PyObject_Del(r);
}

static char*
ropeiter_get_string(RopeObject* rope, int* base_length)
{
  char *retval, *retval_p;
  PyObject* to_str;
  if(rope->type==REPEAT_NODE) {
	*base_length=rope->v.concat.left->length;
	to_str=rope->v.concat.left;
  } else {
	*base_length=rope->length;
	to_str=rope;
  }
  retval=PyMem_Malloc(*base_length);
  retval_p=retval;
  _rope_str(to_str, &retval_p);
  return retval;
}

static PyObject *
ropeiter_next(RopeIter * self)
{
  int base_length; 
  PyObject* retval;

	if (self->pos >= self->rope->length &&
	    self->rope->type != REPEAT_NODE)
	  return NULL;
	if(self->cur_pos>=self->cur_length) {
	  self->cur_pos=0;
	  self->list_pos++;
	  if(self->list_pos>=self->list_length)
		return NULL;
	  PyMem_Free(self->cur);
	  self->cur_length=self->list[self->list_pos]->length;
	  self->base_length=self->cur_length;
	  ropeiter_get_string(self->list[self->list_pos], &self->base_length);
	}
	retval=PyString_FromStringAndSize(&self->cur[self->cur_pos%self->base_length], 1);
	self->cur_pos++;
	self->pos++;
	return retval;
}

static RopeIter *
rope_iter(RopeObject *self)
{
	RopeIter *retval =
		(RopeIter *) PyType_GenericNew(&RopeIter_Type, NULL,
						       NULL);
	if (!retval)
		return NULL;
	Py_INCREF(self);
	retval->pos=0;
	retval->rope=self;
	retval->list_length=rope_get_balance_list_count(self);
	retval->list=PyMem_Malloc(sizeof(struct rope_node *) * retval->list_length);
	_rope_get_balance_list(retval->list, self);
	retval->list_pos=0;
	retval->cur_length=retval->list[0]->length;
	retval->base_length=retval->cur_length;
	retval->cur_pos=0;
	retval->cur=(retval->list_length?ropeiter_get_string(retval->list[0], &retval->base_length):NULL);
	if(PyErr_Occurred())
	  return NULL;
	return retval;
}

/* static PyObject * */
/* ropeobj_slice(RopeObject *self, int start, int end) */
/* { */
/* 	RopeObject *rope; */
/* 	if (end > self->length) */
/* 		end = self->length; */
/* 	if (start > self->length) */
/* 		start = self->length; */
/* 	rope = (RopeObject *)python_rope_new(); */
/* 	if (rope == NULL) */
/* 		return PyErr_NoMemory(); */
/* 	rope->type = SLICE_NODE; */
/* 	rope->length = end - start; */
/* 	rope->v.slice.left = self; */
/* 	rope->v.slice.right = NULL; */
/* 	rope->v.slice.start = start; */
/* 	rope->v.slice.end = end; */
/* 	return (PyObject *)rope; */
/* } */

static int
rope_contains(RopeObject *self, RopeObject *other)
{
	RopeIter *self_iter, *other_iter;
	PyObject *self_cur, *other_cur;
	int i, j, skip;
	i = j = 0;
	if (!Rope_Check(other)) {
		PyErr_SetString(PyExc_TypeError,
				"'in <rope>' requires rope as left operand");
		return -1;
	}
	self_iter = (RopeIter *) PyObject_GetIter((PyObject *)self);
	if (!self_iter)
		return -1;
	other_iter = (RopeIter *) PyObject_GetIter((PyObject *)other);
	if (!other_iter)
		return -1;
	other_cur = PyIter_Next((PyObject *)other_iter);
	for (; i < self->length; i++) {
		self_cur = PyIter_Next((PyObject *)self_iter);
		if (PyObject_Compare(self_cur, other_cur) != 0) {
			Py_XDECREF(self_cur);
			continue;
		}
		for (; j < (other->length - 1); j++) {
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
		Py_XDECREF(self_iter);
		other_iter = (RopeIter *) PyObject_GetIter((PyObject *)
												   other);
		other_cur = PyIter_Next((PyObject *)other_iter);
		self_iter = (RopeIter *) PyObject_GetIter((PyObject *)
												  self);
		for(skip=0;skip<i;skip++){
			self_cur=PyIter_Next(self_iter);
			Py_DECREF(self_cur);
		}
	}
	Py_XDECREF(other_cur);
	Py_XDECREF(self_cur);
	Py_XDECREF(other_iter);
	Py_XDECREF(self_iter);
	return 0;
}

static int
rope_compare(RopeObject *self, RopeObject *other)
{
	int retval = 0;
	int i = 0;
	RopeIter *self_iter, *other_iter;
	PyObject *self_cur, *other_cur;
	if (self->length != other->length) {
		if (self->length < other->length)
			return -1;
		else
			return 1;
	}
	self_iter = (RopeIter *) PyObject_GetIter((PyObject *)self);
	if (!self_iter)
		return -1;
	other_iter = (RopeIter *) PyObject_GetIter((PyObject *)other);
	if (!other_iter)
		return -1;
	for (; i < self->length; i++) {
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

static PySequenceMethods rope_as_sequence = {
  		(inquiry)rope_length,                   /* sq_length */
        (binaryfunc)rope_concat,                /* sq_concat */
        (intargfunc)rope_repeat,                /* sq_repeat */
        (intargfunc)rope_getitem,               /* sq_item */
        0,                                      /* sq_slice */
        0,                                      /* sq_ass_item */
        0,                                      /* sq_ass_slice */
        rope_contains,                          /* sq_contains */
        0,                                      /* sq_inplace_concat */
        0,                                      /* sq_inplace_repeat */
};

/* XXX More documentation */
PyDoc_STRVAR(rope_doc,
			 "Rope type");

PyDoc_STRVAR(ropeiter_doc,
			 "Rope iterator");

static PyTypeObject Rope_Type = {
        PyObject_HEAD_INIT(NULL)
        0,                                              /* ob_size */
        "ropes.Rope",                                   /* tp_name */
        sizeof(RopeObject),                             /* tp_basicsize */
        0,                                              /* tp_itemsize */
        (destructor)rope_dealloc,                       /* tp_dealloc */
        0,                                              /* tp_print */
        0,                                              /* tp_getattr */
        0,                                              /* tp_setattr */
        (cmpfunc) rope_compare,                         /* tp_compare */
        (reprfunc)rope_repr,                            /* tp_repr */
        0,                                              /* tp_as_number */
        &rope_as_sequence,                              /* tp_as_sequence */
        0,                                              /* tp_as_mapping */
        (hashfunc)rope_hash,                            /* tp_hash */
        0,                                              /* tp_call */
        (reprfunc)rope_str,                             /* tp_str */
        PyObject_GenericGetAttr,                        /* tp_getattro */
        0,                                              /* tp_setattro */
        0,                                              /* tp_as_buffer */
        Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,       /* tp_flags */
        rope_doc,                                       /* tp_doc */
        0,                                              /* tp_traverse */
        0,                                              /* tp_clear */
        0,                                              /* tp_richcompare */
        0,                                              /* tp_weaklistoffset */
        rope_iter,                                      /* tp_iter */
        0,                                              /* tp_iternext */
        0,                                              /* tp_methods */
        0,                                              /* tp_members */
        0,                                              /* tp_getset */
        0,                                              /* tp_base */
        0,                                              /* tp_dict */
        0,                                              /* tp_descr_get */
        0,                                              /* tp_descr_set */
        0,                                              /* tp_dictoffset */
        0,                                              /* tp_init */
        0,                                              /* tp_alloc */
        rope_new,                                       /* tp_new */
        0,                                              /* tp_free */
};

static PyTypeObject RopeIter_Type = {
		PyObject_HEAD_INIT(0)	                 /* Must fill in type value later */
		0,                                       /* ob_size */
		"ropes.RopeIter",	                     /* tp_name */
		sizeof(RopeIter),	                     /* tp_	basicsize */
		0,                                       /* tp_itemsize */
		(destructor) ropeiter_dealloc,	         /* tp_dealloc */
		0,			                             /* tp_print */
		0,			                             /* tp_getattr */
		0,			                             /* tp_setattr */
		0,			                             /* tp_compare */
		0,			                             /* tp_repr */
		0,			                             /* tp_as_number */
		0,			                             /* tp_as_sequence */
		0,			                             /* tp_as_mapping */
		0,			                             /* tp_hash */
		0,			                             /* tp_call */
		0,			                             /* tp_str */
		0,			                             /* tp_getattro */
		0,			                             /* tp_setattro */
		0,			                             /* tp_as_buffer */
		Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,/* tp_flags */
		ropeiter_doc,			                 /* tp_doc */
		0,			                             /* tp_traverse */
		0,			                             /* tp_clear */
		0,			                             /* tp_richcompare */
		0,			                             /* tp_weaklistoffset */
		(getiterfunc) PyObject_SelfIter,         /* tp_iter */
		(iternextfunc) ropeiter_next,            /* tp_iternext */
		0,			                             /* tp_methods */
		0,			                             /* tp_members */
		0,			                             /* tp_getset */
		0,			                             /* tp_base */
		0,			                             /* tp_dict */
		0,			                             /* tp_descr_get */
		0,			                             /* tp_descr_set */
		0,			                             /* tp_dictoffset */
		0,			                             /* tp_init */
		PyType_GenericAlloc,			         /* tp_alloc */
		0,			                             /* tp_new */
};

#if DEBUG
static PyObject *
ropeobj_blc(RopeObject *self, PyObject *args, PyObject *kwds)
{
	rope_balance(self);
	Py_RETURN_NONE;
}

static PyObject*
ropeobj_get_type(RopeObject* self)
{
	return PyInt_FromLong(self->type);
}

static PyObject*
ropeobj_get_left(RopeObject* self)
{
	if(self->type!=CONCAT_NODE && self->type!=REPEAT_NODE)
		Py_RETURN_NONE;
	Py_INCREF(self->v.concat.left);
	return (PyObject*) self->v.concat.left;
}

static PyObject*
ropeobj_get_right(RopeObject* self)
{
	if(self->type!=CONCAT_NODE && self->type!=REPEAT_NODE)
		Py_RETURN_NONE;
	Py_INCREF(self->v.concat.right);
	return (PyObject*) self->v.concat.right;
}

static PyObject*
ropeobj_get_count(RopeObject* self)
{
	if(self->type!=REPEAT_NODE)
		Py_RETURN_NONE;
	return PyInt_FromLong(self->v.repeat.count);
}

static PyObject*
ropeobj_get_literal(RopeObject* self)
{
	if(self->type!=LITERAL_NODE)
		Py_RETURN_NONE;
	return PyString_FromStringAndSize(self->v.literal,self->length);
}

static PyMethodDef ropeobj_methods[] = {
	{"balance", (PyCFunction)ropeobj_blc, METH_VARARGS, "internal"},
	{NULL, NULL, 0, NULL}
};

static PyGetSetDef ropeobj_getset[] = {
	{"type",    (getter)ropeobj_get_type,    NULL, NULL, NULL},
	{"left",    (getter)ropeobj_get_left,    NULL, NULL, NULL},
	{"right",   (getter)ropeobj_get_right,   NULL, NULL, NULL},
	{"count",   (getter)ropeobj_get_count,   NULL, NULL, NULL},
	{"literal", (getter)ropeobj_get_literal, NULL, NULL, NULL}
};
#endif  /* DEBUG */

/**** Start of reviewed part *****************************************/

PyMODINIT_FUNC
initropes(void)
{
	PyObject *m;

	if (PyType_Ready(&Rope_Type) < 0)
		return;

	m = Py_InitModule3("ropes", NULL, ropes_module_doc);
	if (DEBUG) {
		PyModule_AddIntConstant(m, "CONCAT_NODE",  CONCAT_NODE);
		PyModule_AddIntConstant(m, "REPEAT_NODE",  REPEAT_NODE);
		PyModule_AddIntConstant(m, "LITERAL_NODE", LITERAL_NODE);
		PyModule_AddIntConstant(m, "SLICE_NODE",   SLICE_NODE);
	}
	Py_INCREF(&Rope_Type);
	PyModule_AddObject(m, "Rope", (PyObject *)&Rope_Type);
}
