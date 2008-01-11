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

/**** Start of reviewed part *****************************************/

#include "Python.h"
#include "limits.h"

#define DEBUG 1
#define LITERAL_MERGING 1
#define MIN_LITERAL_LENGTH 128
#define ROPE_DEPTH 32
#define ROPE_BALANCE_DEPTH 8

/* XXX More documentation */
PyDoc_STRVAR(ropes_module_doc, "Ropes implementation for CPython");

enum node_type {
	LITERAL_NODE,
	CONCAT_NODE,
	REPEAT_NODE,
};

typedef struct RopeObject {
	PyObject_HEAD
	enum node_type type;
	Py_ssize_t length;
	long hash;		/* -1 if not computed. */
	int depth;		/* not used yet. */
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
	} v;
} RopeObject;

typedef struct RopeIter {
	PyObject_HEAD
	RopeObject *rope;
	RopeObject **list;
	Py_ssize_t list_length;
	char *cur;
	Py_ssize_t cur_length;
	Py_ssize_t base_length;
	Py_ssize_t pos, list_pos, cur_pos;
} RopeIter;


static PyTypeObject Rope_Type;
static PyTypeObject RopeIter_Type;

static RopeObject* rope_balance(RopeObject *r);
static RopeObject *rope_slice(RopeObject *self, Py_ssize_t start,
			      Py_ssize_t stop);
static RopeObject *rope_slice_left(RopeObject *self, Py_ssize_t stop);
static RopeObject *rope_slice_right(RopeObject *self, Py_ssize_t start);

#define Rope_Check(op) (((PyObject *)(op))->ob_type == &Rope_Type)

static void
_rope_str(RopeObject *rope, char **p)
{
	int i;
	char *tmp, *q;

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
		/* Compute the child's str once and repeat it */
		tmp = (char *)PyMem_Malloc(rope->length * sizeof(char));
		if (tmp == NULL) {
			PyErr_NoMemory();
			return;
		}
		q = tmp;
		_rope_str(rope->v.repeat.child, &q);
		for (i = 0; i < rope->v.repeat.count; i++) {
			memcpy(*p, tmp, rope->v.repeat.child->length);
			*p += rope->v.repeat.child->length;
		}
		PyMem_Free(tmp);
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
	assert(self && i < self->length);

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

	if (i < 0)
		i += self->length;
	if (i < 0 || i >= self->length) {
		PyErr_SetString(PyExc_IndexError, "rope index out of range");
		return NULL;
	}
	c = rope_index(self, i);
	return PyString_FromStringAndSize(&c, 1);
}

static PyObject *
rope_subscript(RopeObject *self, PyObject * item)
{
	if (PyIndex_Check(item)) {
		Py_ssize_t i = PyNumber_AsSsize_t(item, PyExc_IndexError);

		if (i == -1 && PyErr_Occurred())
			return NULL;
		return rope_getitem(self, i);
	}
	else if (PySlice_Check(item)) {
		Py_ssize_t start, stop, step, length;
		if(PySlice_GetIndicesEx((PySliceObject *)item,
					self->length,
					&start, &stop, &step, &length) < 0) {
			return NULL;
		}
		if(step != 1) {
			Py_INCREF(Py_NotImplemented);
			return Py_NotImplemented;
		}
		return rope_slice(self, start, stop);
	}
	else {
		PyErr_SetString(PyExc_TypeError,
				"Rope indices must be integers");
		return NULL;
	}
}

static void
rope_dealloc(RopeObject *self)
{
	PyObject_GC_UnTrack(self);
	Py_TRASHCAN_SAFE_BEGIN(self)
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
	}
	((PyObject *) self)->ob_type->tp_free(self);
	Py_TRASHCAN_SAFE_END(self)
}

static RopeObject *
rope_from_type(enum node_type type, Py_ssize_t len)
{
	RopeObject *new;

	assert(len >= 0);
	new = PyObject_GC_New(RopeObject, &Rope_Type);
	if (new == NULL)
		return NULL;

	new->type = type;
	new->length = len;
	new->hash = -1;
	new->depth = 1;
	return new;
}

static RopeObject *
rope_from_string(const char *str, Py_ssize_t len)
{
	RopeObject *new;

	new = rope_from_type(LITERAL_NODE, len);
	if (new == NULL)
		return NULL;
	
	new->v.literal = (char *)PyMem_Malloc(len * sizeof(char));
	if (new->v.literal == NULL) {
		PyErr_NoMemory();
		return NULL;
	}
	memcpy(new->v.literal, str, len);

	return new;
}

static PyObject *
rope_new(PyTypeObject * type, PyObject * args, PyObject * kwds)
{
	static char *kwlist[] = { "string", 0 };
	RopeObject *self;
	PyObject *str = NULL;
	const char *literal;
	Py_ssize_t length;

	if (!PyArg_ParseTupleAndKeywords(args, kwds, "|O:Rope", kwlist, &str))
		return NULL;

	if (str == NULL)
		return (PyObject *) rope_from_string("", 0);
	else if (Rope_Check(str)) {
		Py_INCREF(str);
		return str;
	}
	else if (!PyString_Check(str)) {
		PyErr_Format(PyExc_TypeError,
			     "expected string argument, not %.50s",
			     str->ob_type->tp_name);
		return NULL;
	}
	literal = PyString_AS_STRING(str);
	length = PyString_GET_SIZE(str);
	self = rope_from_string(literal, length);

	return (PyObject *) self;
}

static RopeObject *
rope_concat_unchecked(RopeObject *self, RopeObject *other)
{
	RopeObject *result;

	if (!Rope_Check(other)) {
		PyErr_Format(PyExc_TypeError,
			     "cannot concatenate Rope with '%.50s'",
			     ((PyObject *) other)->ob_type->tp_name);
		return NULL;
	}
	Py_INCREF(self);
	Py_INCREF(other);
	result = rope_from_type(CONCAT_NODE, self->length + other->length);
	result->v.concat.left = self;
	result->v.concat.right = other;
	result->depth =
		(result->v.concat.left->depth>result->v.concat.right->depth?
		 result->v.concat.left->depth:result->v.concat.right->depth)+1;

	return result;
}

static RopeObject*
rope_concat(RopeObject* self, RopeObject* other)
{
	RopeObject* result=rope_concat_unchecked(self, other);
	if(result==NULL)
		return NULL;
	if (result->depth > ROPE_BALANCE_DEPTH) {
		RopeObject* balanced=rope_balance(result);
		Py_DECREF(result);
		result=balanced;
	}
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

typedef int (*charproc) (char c, void *arg);

static int
rope_char_iter(RopeObject *self, charproc f, void *arg)
{
	int status = -1;
	Py_ssize_t i;

	switch (self->type) {
	case LITERAL_NODE:
		for (i = 0; i < self->length; i++) {
			status = (*f) (self->v.literal[i], arg);
			if (status == -1)
				return -1;
		}
		break;
	case CONCAT_NODE:
		if (self->v.concat.left)
			status = rope_char_iter(self->v.concat.left, f, arg);
		if (self->v.concat.right)
			status = rope_char_iter(self->v.concat.right, f, arg);
		break;
	case REPEAT_NODE:
		for (i = 0; i < self->v.repeat.count; i++)
			status = rope_char_iter(self->v.repeat.child, f, arg);
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
	rope_char_iter(self, (charproc)_rope_hash, p);
	hash = *p;
	hash ^= self->length;
	if (hash == -1)
		hash = -2;
	self->hash = hash;
	return hash;
}

static int
rope_traverse(RopeObject *self, visitproc visit, void *arg)
{
	switch (self->type) {
	case CONCAT_NODE:
		Py_VISIT(self->v.concat.left);
		Py_VISIT(self->v.concat.right);
		break;
	case REPEAT_NODE:
		Py_VISIT(self->v.repeat.child);
		break;
	case LITERAL_NODE:
		break;
	}
	return 0;
}

static Py_ssize_t
rope_length(RopeObject *self)
{
	return self->length;
}

static void
ropeiter_dealloc(RopeIter *r)
{
	PyMem_Free(r->list);
	Py_DECREF(r->rope);
	PyObject_Del(r);
}

static char *
ropeiter_get_string(RopeObject *rope, int *base_length)
{
	char *retval, *retval_p;
	PyObject *to_str;
	if (rope->type == REPEAT_NODE) {
		*base_length = rope->v.concat.left->length;
		to_str = rope->v.concat.left;
	}
	else {
		*base_length = rope->length;
		to_str = rope;
	}
	retval = PyMem_Malloc(*base_length);
	retval_p = retval;
	_rope_str(to_str, &retval_p);
	return retval;
}

static PyObject *
ropeiter_next(RopeIter *self)
{
	int base_length;
	PyObject *retval;
	if (self->pos >= self->rope->length)
		return NULL;
	if (self->cur_pos >= self->cur_length) {
		self->cur_pos = 0;
		self->list_pos++;
		if (self->list_pos >= self->list_length)
			return NULL;
		PyMem_Free(self->cur);
		self->cur_length = self->list[self->list_pos]->length;
		self->base_length = self->cur_length;
		self->cur =
			ropeiter_get_string(self->list[self->list_pos],
					    &self->base_length);
	}
	retval = PyString_FromStringAndSize(&self->
					    cur[self->cur_pos %
						self->base_length], 1);
	self->cur_pos++;
	self->pos++;
	return retval;
}

PyDoc_STRVAR(ropeiter_doc, "Rope Iterator");

static PyTypeObject RopeIter_Type = {
	PyObject_HEAD_INIT(0)
	0,			/* ob_size */
	"ropes.RopeIter",	/* tp_name */
	sizeof(RopeIter),	/* tp_basicsize */
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
	ropeiter_doc,		/* tp_doc */
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

static int
rope_get_iter_list_count(RopeObject *node)
{
	int retval = 0;
	if (!node)
		return 0;
	if (node->type == CONCAT_NODE)
		retval +=
			rope_get_iter_list_count(node->v.concat.left) +
			rope_get_iter_list_count(node->v.concat.right);
	else
		retval = 1;
	return retval;
}

static int
_rope_get_iter_list(RopeObject **node_list, RopeObject *node)
{
	if (!node)
		return 0;
	if (node->type == CONCAT_NODE) {
		int where = _rope_get_iter_list(node_list,
						   node->v.concat.left);
		where += _rope_get_iter_list(node_list + where,
						node->v.concat.right);
		return where;
	}
	else {
		*node_list = node;
		return 1;
	}
	return 0;
}

static Py_ssize_t
_find_fib_slot(Py_ssize_t length)
{
	Py_ssize_t old_a;
	Py_ssize_t a = 1;
	Py_ssize_t b = 2;
	Py_ssize_t i = 0;
	if(length==0)
		return -1;
	while(1) {
		if(a <= length && length < b)
			return i;
		old_a = a;
		a = b;
		b = old_a + b;
		i++;
	}
}

static RopeObject*
rope_balance(RopeObject* r)
{
	Py_ssize_t blc, i, work_list_length, empty, a,b, old_a ;
	RopeObject** node_list;
	RopeObject** work_list;
	RopeObject *first_node, *cur;
	if(!r || r->type != CONCAT_NODE)
		return;
	a=b=INT_MAX;
	work_list_length = _find_fib_slot(r->length) + 1;
	empty=work_list_length;
	blc = rope_get_iter_list_count(r);
 	node_list = PyMem_Malloc(sizeof(struct RopeObject *) * (blc + 4));
	work_list = PyMem_Malloc(sizeof(struct RopeObject *) * work_list_length);
 	_rope_get_iter_list(node_list, r);
	for(i = 0;i < blc;i++)
		Py_INCREF(node_list[i]);
	#ifdef LITERAL_MERGING
	for (i = 0; i < (blc - 1); i++) {
		if (node_list[i]->type == LITERAL_NODE &&
		    node_list[i + 1]->type == LITERAL_NODE) {
			int length =
				node_list[i]->length +
				node_list[i +1]->length;
			if (length < MIN_LITERAL_LENGTH) {
				RopeObject *cur = node_list[i];
				RopeObject *next = node_list[i + 1];
				cur->v.literal =
					PyMem_Realloc(cur->v.literal, length);
				memcpy(cur->v.literal + cur->length,
				       next->v.literal, next->length);
				cur->length = length;
				Py_DECREF(next);
				if((blc - i - 2) > 0)
				  memcpy(&node_list[i + 1], &node_list[i + 2],
						 (blc - i - 2) *
						 sizeof(struct RopeObject *));
				i--;
				blc--;
			}
		}
	}
	if(blc==1) {
		cur=node_list[0];
		PyMem_Free(node_list);
		PyMem_Free(work_list);
		return cur;
	}
	#endif
	memset(work_list, 0, sizeof(struct RopeObject *) * work_list_length);
	for(i = 0;i < blc;i++) {
		cur = node_list[i];
		if(cur->length < a) {
			a=1;
			b=2;
			empty=0;
			while(! (cur->length < b)) {
				empty++;
				old_a = a;
				a = b;
				b = old_a + b;
			}
		}
		else {
			while(! (cur->length < b &&
				 ! work_list[empty])) {
				if(work_list[empty]) {
					cur = rope_concat_unchecked(work_list[empty], cur);
					assert(empty<work_list_length);
					work_list[empty] = NULL;
				}
				else {
					empty++;
					old_a = a;
					a = b;
					b = old_a + b;
				} 
			}
		}
		assert(empty<work_list_length);
		work_list[empty] = cur;
		first_node = cur;
	}
	cur = first_node;
	for(i = empty+1; i < work_list_length;i++) {
		assert(i<work_list_length);
		if(work_list[i]) {
			cur = rope_concat_unchecked(work_list[i], cur);
		}
	}
	PyMem_Free(node_list);
	PyMem_Free(work_list);
	return cur;
}

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
	self_iter = (RopeIter *)PyObject_GetIter((PyObject *) self);
	if (!self_iter)
		return -1;
	other_iter = (RopeIter *)PyObject_GetIter((PyObject *) other);
	if (!other_iter)
		return -1;
	other_cur = PyIter_Next((PyObject *) other_iter);
	for (; i < self->length; i++) {
		self_cur = PyIter_Next((PyObject *) self_iter);
		if (PyObject_Compare(self_cur, other_cur) != 0) {
			Py_XDECREF(self_cur);
			continue;
		}
		for (; j < (other->length - 1); j++) {
			other_cur = PyIter_Next((PyObject *) other_iter);
			self_cur = PyIter_Next((PyObject *) self_iter);
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
		other_iter = (RopeIter *)PyObject_GetIter((PyObject *)
							  other);
		other_cur = PyIter_Next((PyObject *) other_iter);
		self_iter = (RopeIter *)PyObject_GetIter((PyObject *)
							 self);
		for (skip = 0; skip < i; skip++) {
			self_cur = PyIter_Next(self_iter);
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
	self_iter = (RopeIter *)PyObject_GetIter((PyObject *) self);
	if (!self_iter)
		return -1;
	other_iter = (RopeIter *)PyObject_GetIter((PyObject *) other);
	if (!other_iter)
		return -1;
	for (; i < self->length; i++) {
		self_cur = PyIter_Next((PyObject *) self_iter);
		if (!self_cur)
			return -1;
		other_cur = PyIter_Next((PyObject *) other_iter);
		if (!other_cur)
			return -1;
		retval = PyObject_Compare(self_cur, other_cur);
		if (retval != 0) {
			Py_DECREF(self_iter);
			Py_DECREF(other_iter);
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

static RopeIter *
rope_iter(RopeObject *self)
{
	RopeIter *retval = (RopeIter *)PyType_GenericNew(&RopeIter_Type, NULL,
							 NULL);
	if (!retval)
		return NULL;
	Py_INCREF(self);
	retval->pos = 0;
	retval->rope = self;
	retval->list_length = rope_get_iter_list_count(self);
	retval->list =
		PyMem_Malloc(sizeof(struct RopeObject *) *
			     retval->list_length);
	_rope_get_iter_list(retval->list, self);
	retval->list_pos = 0;
	retval->cur_length = retval->list[0]->length;
	retval->base_length = retval->cur_length;
	retval->cur_pos = 0;
	retval->cur =
		(retval->
		 list_length ? ropeiter_get_string(retval->list[0],
						   &retval->
						   base_length) : NULL);
	if (PyErr_Occurred())
		return NULL;
	return retval;
}

static RopeObject *
rope_slice_right(RopeObject *self, Py_ssize_t start)
{
	while (1) {
		if (start == 0) {
			Py_INCREF(self);
			return self;
		}
		if (self->type == CONCAT_NODE) {
			Py_ssize_t llen = self->v.concat.left->length;
			if (start >= llen) {
				self = self->v.concat.right;
				start = start - llen;
				continue;
			}
			else {
				RopeObject *left, *retval;
				left = rope_slice_right(self->v.concat.left,
							start);
				retval = rope_concat(left,
						     self->v.concat.right);
				Py_DECREF(left);
				return retval;
			}
		}
		return rope_slice(self, start, self->length);
	}
}

static RopeObject *
rope_slice_left(RopeObject *self, Py_ssize_t stop)
{
	while (1) {
		if (stop == self->length) {
			Py_INCREF(self);
			return self;
		}
		if (self->type == CONCAT_NODE) {
			Py_ssize_t llen = self->v.concat.left->length;
			if (stop <= llen) {
				self = self->v.concat.left;
				continue;
			}
			else {
				RopeObject *right, *retval;
				right = rope_slice_left(self->v.concat.right,
							stop - llen);
				retval = rope_concat(self->v.concat.left,
						     right);
				Py_DECREF(right);
				return retval;
			}
		}
		return rope_slice(self, 0, stop);
	}
}

static RopeObject *
rope_slice(RopeObject *self, Py_ssize_t start, Py_ssize_t stop)
{
	RopeObject *retval;
	Py_ssize_t adj_start, adj_stop;
	if (stop > self->length)
		stop = self->length;
	if (start >= self->length) {
		PyErr_SetString(PyExc_ValueError, "No sane value to slice!");
		return NULL;
	}
	switch (self->type) {
	case LITERAL_NODE:
		retval = rope_from_string(self->v.literal + start,
					  stop - start);
		break;
	case REPEAT_NODE:
		/* The basic procedure here is to first find how much the new
		 * repeat node needs to be repeated. Then we need to create two
		 * new concatenation nodes, then two new literal nodes, one for
		 * the leftest node and one for the rightest node. Then concatenate
		 * them together
		 */
		retval = rope_from_type(REPEAT_NODE, 0);
		retval->v.repeat.child = self->v.repeat.child;
		Py_INCREF(retval->v.repeat.child);
		adj_start =
			(start % self->v.repeat.child->length ? start +
			 (self->v.repeat.child->length -
			  (start % self->v.repeat.child->length)) : start);
		adj_stop = stop - (stop % self->v.repeat.child->length);
		retval->v.repeat.count =
			((adj_stop -
			  adj_start) / self->v.repeat.child->length);
		retval->length =
			retval->v.repeat.count *
			retval->v.repeat.child->length;
		if (retval->v.repeat.count <= 0) {
			RopeObject *left, *right, *old_retval;
			old_retval = retval;
			left = right = NULL;
			left = rope_slice(retval->v.repeat.child,
					  (start %
					   retval->v.repeat.child->length),
					  start + (stop - start));
			retval = left;
			if (left->length == (stop - start))
				break;
			if ((stop % old_retval->v.repeat.child->length) != 0) {
				right = rope_slice(old_retval->v.repeat.child,
						   0,
						   (stop %
						    old_retval->v.repeat.
						    child->length));
				if (left && right) {
					retval = rope_concat(left, right);
					Py_DECREF(right);
					Py_DECREF(left);
				}
				else if (right) {
					retval = right;
				}
				else {
					retval = left;
				}
			}
			Py_DECREF(old_retval);
		}
		else {
			RopeObject *start_node, *end_node, *old_retval;
			old_retval = retval;
			if ((start % retval->v.repeat.child->length) != 0) {
				start_node =
					rope_slice(retval->v.repeat.child,
						   (start %
						    retval->v.repeat.child->
						    length),
						   old_retval->v.repeat.child->
						   length);
				retval = rope_concat(start_node, retval);
				Py_DECREF(start_node);	/* Because their refcounts were increased in rope_concat */
				Py_DECREF(old_retval);
			}
			if ((stop % old_retval->v.repeat.child->length) != 0) {
				end_node =
					rope_slice(old_retval->v.repeat.child,
						   0,
						   (stop %
						    old_retval->v.repeat.
						    child->length));
				old_retval = retval;
				retval = rope_concat(retval, end_node);
				Py_DECREF(end_node);
				Py_DECREF(old_retval);
			}
		}
		break;
	case CONCAT_NODE:
		if (start == 0) {
			if (stop == self->length) {
				Py_INCREF(self);
				retval = self;
			}
			return rope_slice_left(self, stop);
		}
		else if (stop == self->length) {
			return rope_slice_right(self, start);
		}
		else {
			RopeObject *left, *right;
			if (stop < self->v.concat.left->length) {
				return rope_slice(self->v.concat.left, start,
						  stop);
			}
			else if (start > self->v.concat.left->length) {
				return rope_slice(self->v.concat.right,
						  start -
						  self->v.concat.left->length,
						  stop -
						  self->v.concat.left->length);
			}
			left = rope_slice_right(self->v.concat.left, start);
			right = rope_slice_left(self->v.concat.right,
						stop -
						self->v.concat.left->length);
			if (left && right) {
				retval = rope_concat(left, right);
				Py_DECREF(left);
				Py_DECREF(right);
			}
			else if (right) {
				retval = right;
			}
			else {
				retval = left;
			}
		}
		break;
	}
	return retval;
}

static PySequenceMethods rope_as_sequence = {
	(inquiry) rope_length,		/* sq_length */
	(binaryfunc) rope_concat,	/* sq_concat */
	(ssizeargfunc) rope_repeat,	/* sq_repeat */
	(ssizeargfunc) rope_getitem,	/* sq_item */
	0,				/* sq_slice */
	0,				/* sq_ass_item */
	0,				/* sq_ass_slice */
	(inquiry) rope_contains,	/* sq_contains */
	0,				/* sq_inplace_concat */
	0,				/* sq_inplace_repeat */
};

static PyMappingMethods rope_as_mapping = {
	(inquiry) rope_length,		/* mp_length */
	(binaryfunc) rope_subscript,	/* mp_subscript */
	0,				/* mp_ass_subscript */
};

/* XXX More documentation */
PyDoc_STRVAR(rope_doc, "Rope type");

#if DEBUG
static PyObject *
rope_balance_method(PyObject * self, PyObject * args, PyObject * kwds)
{
	RopeObject* retval = rope_balance(self);
	return retval;
}

static PyMethodDef RopeMethods[] = {
	{"balance", rope_balance_method, METH_VARARGS, "Balance the rope"},
	{NULL, NULL, NULL}
};
#endif

static PyTypeObject Rope_Type = {
	PyObject_HEAD_INIT(NULL)
		0,		/* ob_size */
	"ropes.Rope",		/* tp_name */
	sizeof(RopeObject),	/* tp_basicsize */
	0,			/* tp_itemsize */
	(destructor) rope_dealloc, /* tp_dealloc */
	0,			/* tp_print */
	0,			/* tp_getattr */
	0,			/* tp_setattr */
	rope_compare,		/* tp_compare */
	(reprfunc) rope_repr,	/* tp_repr */
	0,			/* tp_as_number */
	&rope_as_sequence,	/* tp_as_sequence */
	&rope_as_mapping,	/* tp_as_mapping */
	(hashfunc) rope_hash,	/* tp_hash */
	0,			/* tp_call */
	(reprfunc) rope_str,	/* tp_str */
	PyObject_GenericGetAttr,/* tp_getattro */
	0,			/* tp_setattro */
	0,			/* tp_as_buffer */
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC,	/* tp_flags */
	rope_doc,		/* tp_doc */
	(traverseproc) rope_traverse,	/* tp_traverse */
	0,			/* tp_clear */
	0,			/* tp_richcompare */
	0,			/* tp_weaklistoffset */
	rope_iter,		/* tp_iter */
	0,			/* tp_iternext */
#if DEBUG
	RopeMethods,		/* tp_methods */
#else
	0,			/* tp_methods */
#endif
	0,			/* tp_members */
	0,			/* tp_getset */
	0,			/* tp_base */
	0,			/* tp_dict */
	0,			/* tp_descr_get */
	0,			/* tp_descr_set */
	0,			/* tp_dictoffset */
	0,			/* tp_init */
	0,			/* tp_alloc */
	rope_new,		/* tp_new */
	0,			/* tp_free */
};

PyMODINIT_FUNC
initropes(void)
{
	PyObject *m;

	if (PyType_Ready(&Rope_Type) < 0)
		return;
	if (PyType_Ready(&RopeIter_Type) < 0)
		return;

	m = Py_InitModule3("ropes", NULL, ropes_module_doc);
	if (DEBUG) {
		PyModule_AddIntConstant(m, "CONCAT_NODE", CONCAT_NODE);
		PyModule_AddIntConstant(m, "REPEAT_NODE", REPEAT_NODE);
		PyModule_AddIntConstant(m, "LITERAL_NODE", LITERAL_NODE);
	}
	Py_INCREF(&Rope_Type);
	PyModule_AddObject(m, "Rope", (PyObject *) & Rope_Type);
}
