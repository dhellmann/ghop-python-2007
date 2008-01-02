/*
 * python.c
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
#include "ropes.h"

static PyTypeObject ropes_type;

PyObject*
python_rope_new()
{
  PyObject* tuple=PyTuple_New(0);
  PyObject* dict=PyDict_New();
  PyRopeObject* new_rope=(PyRopeObject*)PyObject_Call((PyObject*) &ropes_type,tuple,dict);
  new_rope->n_type=ROPE_UNINITIALIZED_NODE;
  Py_DECREF(tuple);
  Py_DECREF(dict);
  return (PyObject*)new_rope;
}

static PyRopeObject* PyObject_to_rope_node(PyObject* obj)
{
  if(obj->ob_type==&ropes_type) {
	return (PyRopeObject*) obj;
  }
  else if(PyString_Check(obj) && PyString_Size(obj)<LITERAL_LENGTH) {
	PyRopeObject* ln=(PyRopeObject*)python_rope_new();
	ln->n_type=ROPE_LITERAL_NODE;
	ln->n_length=strlen(PyString_AsString(obj));
	ln->n_node.literal.l_literal=malloc(PyString_Size(obj));
	memcpy(ln->n_node.literal.l_literal, PyString_AsString(obj), ln->n_length);
	return ln;
  }
  else if(PyString_Check(obj)) {
	PyRopeObject* retval=(PyRopeObject*)python_rope_new();
	retval->n_type=ROPE_UNINITIALIZED_NODE;
	char* c=PyString_AsString(obj);
	int size=strlen(c);
	int i=0;
	for(;i<((size/LITERAL_LENGTH)+1);i++) {
	  PyRopeObject* ln;
	  int cur_size=strlen(c)>LITERAL_LENGTH?LITERAL_LENGTH:strlen(c);
	  if(cur_size==0)
		break;
	  ln=(PyRopeObject*) python_rope_new();
	  ln->n_type=ROPE_LITERAL_NODE;
	  ln->n_length=cur_size;
	  ln->n_node.literal.l_literal=malloc(cur_size);
	  memcpy(ln->n_node.literal.l_literal,c,cur_size);
	  rope_append(retval, ln);
	  Py_DECREF(ln);
	  c+=LITERAL_LENGTH;
	}
	rope_balance(retval);
	return retval;
  }
  return NULL;
}

static void
ropeobj_dealloc(PyRopeObject* r)
{
  if(r->n_type!=ROPE_LITERAL_NODE) {
	Py_XDECREF(r->n_node.concat.left); /* If it goes to 0, this function will recurse */
	Py_XDECREF(r->n_node.concat.right);
  } else {
	free(r->n_node.literal.l_literal);
  }
  PyObject_Del((PyObject*)r);
}

static PyObject*
ropeobj_new(PyTypeObject* type, PyObject* args, PyObject* kwds)
{
  PyRopeObject* new;
  new=(PyRopeObject*) type->tp_alloc(type, 0);
  new->n_type=ROPE_UNINITIALIZED_NODE;
  new->n_length=0;
  return (PyObject*)new;
}

static PyObject*
ropeobj_append(PyRopeObject* self, PyObject* args, PyObject* kwds)
{
  int i=0;
  int size=PyTuple_Size(args);
  for(;i<size;i++) {
	PyObject* cur=PyTuple_GetItem(args, i);
	rope_append(self, PyObject_to_rope_node(cur));
  }
  Py_RETURN_NONE;
}

static PyObject*
ropeobj_concat(PyRopeObject* self, PyObject* to_append)
{
  if(self->ob_type!=&ropes_type) {
	PyErr_SetString(PyExc_TypeError, "bad type!");
	return NULL;
  }
  rope_append(self, PyObject_to_rope_node(to_append));
  Py_INCREF(self);
  return (PyObject*)self;
}

static int
ropeobj_initobj(PyRopeObject* self, PyObject* args, PyObject* kwds)
{
  if(self->n_type!=ROPE_LITERAL_NODE)
	{
	  Py_XDECREF(self->n_node.concat.left);
	  Py_XDECREF(self->n_node.concat.right);
	}
  self->n_type=ROPE_UNINITIALIZED_NODE;
  ropeobj_append(self, args, kwds);
  return 0;
}

static PyObject*
ropeobj_str(PyRopeObject* self)
{
  PyObject* retval;
  char* v=malloc(self->n_length);
  rope_to_string(self, v,0,0,self->n_length);
  retval=PyString_FromStringAndSize(v, self->n_length);
  free(v);
  return retval;
}

static PyObject*
ropeobj_repr(PyRopeObject* r)
{
  PyObject* retval;
  char* v=malloc(r->n_length+strlen("Rope('')"));
  memcpy(v, "Rope('", strlen("Rope('"));
  rope_to_string(r, v+strlen("Rope('"), 0,0,r->n_length);
  memcpy(v+strlen("Rope('")+r->n_length, "')",2);
  retval=PyString_FromStringAndSize(v,r->n_length+strlen("Rope('')"));
  free(v);
  return retval;
}

static PyObject*
ropeobj_concat_new(PyObject* self_obj, PyObject* to_append)
{
  if(self_obj->ob_type!=&ropes_type) {
	PyErr_BadInternalCall();
	return NULL;
  }
  PyRopeObject* self=(PyRopeObject*) self_obj;
  PyRopeObject* new_rope=(PyRopeObject*) python_rope_new();
  PyRopeObject* node=PyObject_to_rope_node(to_append);
  if(!node) {
	PyErr_SetString(PyExc_TypeError, "Cannot concatenate rope with anything other than a string or a rope!");
	return NULL;
  }
  Py_INCREF(new_rope);
  rope_append(new_rope,self);
  rope_append(new_rope,node);
  return (PyObject*) new_rope;
}

static PyObject*
ropeobj_repeat_new(PyObject* self_obj, int by)
{
  if(by<0) {
	PyErr_SetString(PyExc_TypeError, "Can't repeat by a negative number");
	return NULL;
  }
  if(self_obj->ob_type!=&ropes_type) {
	PyErr_BadInternalCall();
	return NULL;
  }
  PyRopeObject* self=(PyRopeObject*) self_obj;
  if(self->n_type==ROPE_UNINITIALIZED_NODE || self->n_length==0) {
	PyErr_SetString(PyExc_ValueError, "Can't repeat empty rope");
  }
  rope_incref(self);
  PyRopeObject* new_rope=(PyRopeObject*) python_rope_new();
  new_rope->n_type=ROPE_MULTIPLY_NODE;
  new_rope->n_length=self->n_length*by;
  new_rope->n_node.multiply.m_times=by;
  new_rope->n_node.multiply.right=NULL;
  new_rope->n_node.multiply.left=self;
  return (PyObject*) new_rope;
}

static PyObject*
ropeobj_repeat(PyRopeObject* self, int times)
{
  if(self->ob_type!=&ropes_type) {
	PyErr_BadInternalCall();
	return NULL;
  }
  if(times<0) {
	PyErr_SetString(PyExc_TypeError, "Can't repeat by a negative number!");
	return NULL;
  }
  PyRopeObject* old_root=(PyRopeObject*)python_rope_new();
  rope_move(old_root, self);
  /* TODO: split old root into balanced halves */
  self->n_type=ROPE_MULTIPLY_NODE;
  self->n_length=old_root->n_length*times;
  self->n_node.multiply.left=old_root;
  self->n_node.multiply.right=NULL;
  self->n_node.multiply.m_times=times;
  rope_incref(old_root);
  Py_INCREF(self);
  return (PyObject*) self;
}

static int
ropeobj_length(PyRopeObject* r)
{
  return r->n_length;
}

static PyObject*
ropeobj_blc(PyRopeObject* self, PyObject* args, PyObject* kwds)
{
  rope_balance(self);
  Py_RETURN_NONE;
}

static PyObject*
ropeobj_get_type(PyRopeObject* self)
{
  return PyInt_FromLong(self->n_type);
}

static PyObject*
ropeobj_get_left(PyRopeObject* self)
{
  if(self->n_type!=ROPE_CONCAT_NODE && self->n_type!=ROPE_MULTIPLY_NODE)
	Py_RETURN_NONE;
  Py_INCREF(self->n_node.concat.left);
  return (PyObject*) self->n_node.concat.left;
}

static PyObject*
ropeobj_get_right(PyRopeObject* self)
{
  if(self->n_type!=ROPE_CONCAT_NODE && self->n_type!=ROPE_MULTIPLY_NODE)
	Py_RETURN_NONE;
  Py_INCREF(self->n_node.concat.right);
  return (PyObject*) self->n_node.concat.right;
}

static PyObject*
ropeobj_get_times(PyRopeObject* self)
{
  if(self->n_type!=ROPE_MULTIPLY_NODE)
	Py_RETURN_NONE;
  return PyInt_FromLong(self->n_node.multiply.m_times);
}

static PyObject*
ropeobj_get_literal(PyRopeObject* self)
{
  if(self->n_type!=ROPE_LITERAL_NODE)
	Py_RETURN_NONE;
  return PyString_FromStringAndSize(self->n_node.literal.l_literal,self->n_length);
}

static PyObject*
ropeobj_item(PyRopeObject* self, int i)
{
  if(i>=self->n_length) {
	PyErr_SetString(PyExc_IndexError, "rope index out of range");
	return NULL;
  }
  char c=rope_index(self, i);
  return PyString_FromStringAndSize(&c, 1);
}

static PyObject*
ropeobj_slice(PyRopeObject* self, int start, int end)
{
  PyRopeObject* rope;
  if(end>self->n_length)
	end=self->n_length;
  if(start>self->n_length)
	start=self->n_length;
  rope=python_rope_new();
  if(rope==NULL)
	return PyErr_NoMemory();
  rope->n_type=ROPE_SLICE_NODE;
  rope->n_length=end-start;
  rope->n_node.slice.left=self;
  rope->n_node.slice.right=NULL;
  rope->n_node.slice.start=start;
  rope->n_node.slice.end=end;
  return (PyObject*) rope;
}

static PyMethodDef ropes_methods[] =
  {
	{NULL,NULL,0,NULL}
  };

static PyMethodDef ropeobj_methods[] =
  {
	{"append", (PyCFunction)ropeobj_append, METH_VARARGS, "appends two ropes together"},
	{"balance", (PyCFunction)ropeobj_blc, METH_VARARGS, "internal"},
	{NULL, NULL, 0, NULL}
  };

static PySequenceMethods ropeobj_as_sequence =
  {
	(inquiry) ropeobj_length, /* sq_length */
	(binaryfunc)ropeobj_concat_new, /* sq_concat */
	(intargfunc)ropeobj_repeat_new, /* sq_repeat */
	(intargfunc) ropeobj_item, /* sq_item */
	(intintargfunc) ropeobj_slice, /* sq_slice */
	NULL, /* sq_ass_item */
	NULL, /* sq_ass_slice */
	NULL, /* sq_contains */
	(binaryfunc) ropeobj_concat, /* sq_inplace_concat */
	(intargfunc) ropeobj_repeat, /* sq_inplace_repeat */
  };

static PyGetSetDef ropeobj_getset[] =
  {
	{"type", (getter)ropeobj_get_type, NULL, "Get node type", NULL},
	{"left", (getter)ropeobj_get_left, NULL, "Get the node to the left", NULL},
	{"right", (getter)ropeobj_get_right, NULL, "Get the node to the right", NULL},
	{"times", (getter)ropeobj_get_times, NULL, "Get the amount of repetition", NULL},
	{"literal", (getter)ropeobj_get_literal, NULL, "Get the literal string", NULL}
  };

static PyTypeObject ropes_type=
  {
	PyObject_HEAD_INIT(0)	/* Must fill in type value later */
	0,					/* ob_size */
	"ropes.Rope",			/* tp_name */
	sizeof(PyRopeObject),		/* tp_basicsize */
	0,					/* tp_itemsize */
	(destructor)ropeobj_dealloc,		/* tp_dealloc */
	0,					/* tp_print */
	0,					/* tp_getattr */
	0,					/* tp_setattr */
	(cmpfunc) 0,					/* tp_compare */
	(reprfunc)ropeobj_repr,			/* tp_repr */
	0,					/* tp_as_number */
	&ropeobj_as_sequence,					/* tp_as_sequence */
	0,					/* tp_as_mapping */
	0,					/* tp_hash */
	0,					/* tp_call */
	(reprfunc)ropeobj_str,					/* tp_str */
	PyObject_GenericGetAttr,		/* tp_getattro */
	0,					/* tp_setattro */
	0,					/* tp_as_buffer */
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE , /* tp_flags */
	"A Rope type",				/* tp_doc */
	0,					/* tp_traverse */
	0,					/* tp_clear */
	0,					/* tp_richcompare */
	0,					/* tp_weaklistoffset */
	0,					/* tp_iter */
	0,					/* tp_iternext */
	ropeobj_methods,				/* tp_methods */
	0,					/* tp_members */
	ropeobj_getset,					/* tp_getset */
	0,					/* tp_base */
	0,					/* tp_dict */
	0,					/* tp_descr_get */
	0,					/* tp_descr_set */
	0,					/* tp_dictoffset */
	ropeobj_initobj,				/* tp_init */
	PyType_GenericAlloc,			/* tp_alloc */
	ropeobj_new,				/* tp_new */
	PyObject_Del,				/* tp_free */
  };

PyMODINIT_FUNC
initropes()
{
  PyObject *ropes_module;

  ropes_type.ob_type = &PyType_Type;
  
  ropes_module=Py_InitModule3("ropes", ropes_methods, "Ropes implementation for CPython");
  PyModule_AddIntConstant(ropes_module, "ROPE_UNINITIALIZED_NODE", ROPE_UNINITIALIZED_NODE);
  PyModule_AddIntConstant(ropes_module, "ROPE_CONCAT_NODE", ROPE_CONCAT_NODE);
  PyModule_AddIntConstant(ropes_module, "ROPE_MULTIPLY_NODE", ROPE_MULTIPLY_NODE);
  PyModule_AddIntConstant(ropes_module, "ROPE_LITERAL_NODE", ROPE_LITERAL_NODE);
  PyModule_AddIntConstant(ropes_module, "ROPE_SLICE_NODE", ROPE_SLICE_NODE);
  Py_INCREF((PyObject *)&ropes_type);
  if(PyModule_AddObject(ropes_module, "Rope", (PyObject *) &ropes_type) !=0)
	return;
}
