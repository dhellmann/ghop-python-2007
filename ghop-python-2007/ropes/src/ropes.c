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
#include "ropes.h"
#include "stdlib.h"

static void _rope_inc_refcount(PyRopeObject* rn)
{
  Py_INCREF(rn);
}

void rope_traverse(PyRopeObject* node, rope_traverse_func func)
{
  if(!node)
	return;
  if(node->n_type==ROPE_CONCAT_NODE || node->n_type==ROPE_MULTIPLY_NODE) {
	rope_traverse(node->n_node.concat.left,func);
	rope_traverse(node->n_node.concat.right,func);
  }
  func(node);
}

void rope_incref(PyRopeObject*  r)
{
  rope_traverse(r, _rope_inc_refcount);
}

void rope_move(PyRopeObject* dest, PyRopeObject* src)
{ 
  dest->n_type=src->n_type;
  dest->n_length=src->n_length;
  switch(dest->n_type) {
  case ROPE_CONCAT_NODE:
	dest->n_node.concat.left=src->n_node.concat.left;
	dest->n_node.concat.right=src->n_node.concat.right;
	rope_incref(dest->n_node.concat.left);
	rope_incref(dest->n_node.concat.right);
	break;
  case ROPE_MULTIPLY_NODE:
	dest->n_node.concat.left=src->n_node.concat.left;
	dest->n_node.concat.right=src->n_node.concat.right;
	rope_incref(dest->n_node.concat.left);
	rope_incref(dest->n_node.concat.right);
	dest->n_node.multiply.m_times=src->n_node.multiply.m_times;
	break;
  case ROPE_SLICE_NODE:
	dest->n_node.concat.left=src->n_node.concat.left;
	dest->n_node.concat.right=src->n_node.concat.right;
	rope_incref(dest->n_node.concat.left);
	rope_incref(dest->n_node.concat.right);
	dest->n_node.slice.start=src->n_node.slice.start;
	dest->n_node.slice.end=src->n_node.slice.end;
	break;
  case ROPE_LITERAL_NODE:
	dest->n_node.literal.l_literal=src->n_node.literal.l_literal;
	break;
  }
}

int rope_append(PyRopeObject* r, PyRopeObject* to_append)
{
  PyRopeObject* cur=r;
  PyRopeObject* cur_parent=NULL;
  if(!cur || !to_append || to_append->n_type==ROPE_UNINITIALIZED_NODE)
	return 0;
  if(r->n_type==ROPE_UNINITIALIZED_NODE) {
	rope_move(r, to_append);
	return 0;
  }
  if(r->n_type!=ROPE_CONCAT_NODE) {
	PyRopeObject* new=(PyRopeObject*) python_rope_new();
	rope_move(new, r);
	r->n_type=ROPE_CONCAT_NODE;
	r->n_node.concat.left=new;
	rope_incref(to_append);
	r->n_node.concat.right=to_append;
	r->n_length=r->n_node.concat.left->n_length+r->n_node.concat.right->n_length;
	return 0;
  }
  while(1) {
	if(cur->n_type!=ROPE_CONCAT_NODE || !cur->n_node.concat.right)
	  break;
	cur_parent=cur;
	cur->n_length+=to_append->n_length;
	cur=cur->n_node.concat.right;
  }
  if(cur->n_type==ROPE_CONCAT_NODE) {
	rope_incref(to_append);
	if(!cur->n_node.concat.left && !cur->n_node.concat.right) {
	  cur->n_node.concat.left=to_append;
	  cur->n_length=to_append->n_length;
	  return 0;
	}
	else if(cur->n_node.concat.left && !cur->n_node.concat.right) {
	  cur->n_node.concat.right=to_append;
	  return 0;
	}
	return -1;
  }
  /* cur needs to become a concatenation node */
  else {
	if(cur->ob_refcnt==1)
	  {
		PyRopeObject* new=(PyRopeObject*) python_rope_new();
		rope_move(new, cur);
		cur->n_type=ROPE_CONCAT_NODE;
		cur->n_node.concat.left=new;
		rope_incref(to_append);
		cur->n_node.concat.right=to_append;
		cur->n_length=cur->n_node.concat.left->n_length+cur->n_node.concat.right->n_length;
	  }
	else /* Create new nodes */
	  {
		cur_parent->n_node.concat.right=python_rope_new();
		cur_parent->n_node.concat.right->n_type=ROPE_CONCAT_NODE;
		cur_parent->n_node.concat.right->n_node.concat.left=cur;
		rope_incref(to_append);
		cur_parent->n_node.concat.right->n_node.concat.right=to_append;
		cur_parent->n_node.concat.right->n_length=cur_parent->n_node.concat.right->n_node.concat.left->n_length+cur_parent->n_node.concat.right->n_node.concat.right->n_length;
	  }
	return 0;
  }
  return 0;
}

void rope_to_string(PyRopeObject* node, char* v, unsigned int w, int offset, int length)
{
  if(!node || node->n_length==0)
	return;
  if(offset>node->n_length)
	return;
  if(offset<0) offset=0;
  if(length<0) length=0;
  int whole_length=length;
  switch(node->n_type) {
  case ROPE_SLICE_NODE:
	offset=node->n_node.slice.start;
	length=node->n_length;
	node=node->n_node.slice.left;
  case ROPE_CONCAT_NODE:
	if(offset<=node->n_node.concat.left->n_length)
	  {
		rope_to_string(node->n_node.concat.left, v, w, offset, length);
		length-=(node->n_node.concat.left->n_length-offset);
		w+=(node->n_node.concat.left->n_length-offset);
	  }
	offset-=(node->n_node.concat.left->n_length);
	rope_to_string(node->n_node.concat.right, v, w, offset, length);
	break;
	/* XXX: slice support */
  case ROPE_MULTIPLY_NODE:
	{
	  int i=0;
	  for(;i<node->n_node.multiply.m_times;i++) {
		if(node->n_node.multiply.left) {
		  rope_to_string(node->n_node.multiply.left, v, w,0,node->n_node.multiply.left->n_length);
		  w+=node->n_node.multiply.left->n_length;
		}
		if(node->n_node.multiply.right) {
		  rope_to_string(node->n_node.multiply.right, v, w,0,node->n_node.multiply.right->n_length);
		  w+=node->n_node.multiply.right->n_length;
		}
	  }
	}
	break;
  case ROPE_LITERAL_NODE:
	memcpy(v+w, node->n_node.literal.l_literal+offset, ((node->n_length<length)?node->n_length:length));
	break;
  default: break;
  }
}

char rope_index(PyRopeObject* r, int i)
{
  volatile PyRopeObject* rope=r;
  volatile int index=i;
  /* TAIL RECURSION!!!!!!! */
 head:
  if(!rope || index>rope->n_length)
	return 0;
  switch(rope->n_type) {
  case ROPE_CONCAT_NODE:
	if(rope->n_node.concat.left) {
	  if(index<rope->n_node.concat.left->n_length) {
		rope=rope->n_node.concat.left;
		goto head;
	  }
	  else {
		index=index-rope->n_node.concat.left->n_length;
		rope=rope->n_node.concat.right;
		goto head;
	  }
	}
	else if(rope->n_node.concat.right) {
	  rope=rope->n_node.concat.right;
	  goto head;
	}
	return 0;
  case ROPE_MULTIPLY_NODE:
	{
	  int rope_base_length=0;
	  if(rope->n_node.multiply.left)
		rope_base_length+=rope->n_node.multiply.left->n_length;
	  if(rope->n_node.multiply.right)
		rope_base_length+=rope->n_node.multiply.right->n_length;
	  index=index%rope_base_length;
	  if(rope->n_node.multiply.left) {
		if(index<rope->n_node.multiply.left->n_length) {
		  rope=rope->n_node.multiply.left;
		  goto head;
		}
		else {
		  index=index-rope->n_node.multiply.left->n_length;
		  rope=rope->n_node.multiply.right;
		  goto head;
		}
	  }
	  else if(rope->n_node.concat.right) {
		rope=rope->n_node.concat.right;
		goto head;
	  }
	}
	return 0;
  case ROPE_LITERAL_NODE:
	return rope->n_node.literal.l_literal[index];
  default: return 0;
  }
}

void print_rope(PyRopeObject* node)
{
  if(!node)
	return;
  switch(node->n_type) {
  case ROPE_CONCAT_NODE:
	print_rope(node->n_node.concat.left);
	print_rope(node->n_node.concat.right);
	break;
  case ROPE_MULTIPLY_NODE:
	{
	  int i=0;
	  for(;i<node->n_node.multiply.m_times;i++)
		{
		  print_rope(node->n_node.multiply.left);
		  print_rope(node->n_node.multiply.right);
		}
	}
	break;
  case ROPE_LITERAL_NODE:
	{
	  fwrite(node->n_node.literal.l_literal, node->n_length, 1, stdout);
	}
  default: break;
  }
  return;
}

int rope_get_balance_list_count(PyRopeObject* node)
{
  int retval=0;
  if(!node)
	return 0;
  if(node->n_type==ROPE_CONCAT_NODE)
	retval+=rope_get_balance_list_count(node->n_node.concat.left)+rope_get_balance_list_count(node->n_node.concat.right);
  else
	retval=1;
  return retval;
}

void _rope_balance(PyRopeObject** parent, PyRopeObject** node_list, int node_list_size)
{
  /* If there is only one node just set it */
  if(node_list_size==1) {
	*parent=node_list[0];
	return;
  }
  else {
	/* Otherwise we need a concatenation node */
	if(!(*parent)) {
	  *parent=(PyRopeObject*) python_rope_new();
	  (*parent)->n_type=ROPE_CONCAT_NODE;
	}
	if(node_list_size==2) {
	  /* set the left and rights */
	  (*parent)->n_node.concat.left=node_list[0];
	  (*parent)->n_node.concat.right=node_list[1];
	}
	else {
	  int half_node_list_size=node_list_size/2;
	  int other_half_node_list_size=node_list_size-half_node_list_size;
	  (*parent)->n_node.concat.left=(*parent)->n_node.concat.right=NULL;
	  /* Fill in the left and rights */
	  _rope_balance(&(*parent)->n_node.concat.left, node_list, half_node_list_size);
	  _rope_balance(&(*parent)->n_node.concat.right, node_list+half_node_list_size, other_half_node_list_size);
	}
	(*parent)->n_length=(*parent)->n_node.concat.left->n_length+((*parent)->n_node.concat.right?(*parent)->n_node.concat.right->n_length:0);
  }
}

int _rope_get_balance_list(PyRopeObject** node_list,  PyRopeObject* node)
{
  if(!node)
	return 0;
  if(node->n_type==ROPE_CONCAT_NODE) {
	int where=_rope_get_balance_list(node_list, node->n_node.concat.left);
	where+=_rope_get_balance_list(node_list+where, node->n_node.concat.right);
	return where;
  }
  else {
	*node_list=node;
	return 1;
  }
  return 0;
}

/* This function increments the reference counts of all nodes that are not concatenation nodes. When we delete the concatenation nodes later
   None of the literal or other node types will be deleted because their reference counts are > 1 */
void _rope_balance_del(PyRopeObject* node)
{
  if(!node)
	return;
  if(node->n_type==ROPE_CONCAT_NODE) {
	_rope_balance_del(node->n_node.concat.left);
	_rope_balance_del(node->n_node.concat.right);
  }
  else {
	Py_INCREF(node);
  }
  return;
}

void rope_balance(PyRopeObject* r)
{
  if(!r || r->n_type!=ROPE_CONCAT_NODE)
	return;
  int blc=rope_get_balance_list_count(r);
  PyRopeObject* old_root=r;
  PyRopeObject** node_list=malloc(sizeof(struct rope_node*) * blc);
  _rope_get_balance_list(node_list, r);
  /* Delete the concatenation nodes */
  _rope_balance_del(r->n_node.concat.left);
  _rope_balance_del(r->n_node.concat.right);
  Py_XDECREF(r->n_node.concat.left);
  Py_XDECREF(r->n_node.concat.right);
  r->n_node.concat.left=r->n_node.concat.right=NULL;
  /*   for(;i<blc;i++) */
  /* 	{ */
  /* 	  if(node_list[i]->n_type!=ROPE_LITERAL_NODE) */
  /* 		_rope_balance_node(node_list[i]); */
  /* 	} */
  /* XXX: Get rebalancing to make sure every node is filled in with the full LITERAL_LENGTH bytes of data */
  _rope_balance(&r, node_list, blc);
  free(node_list);
}

int masked_power(int a, int b)
{
  printf("b: %d\n", b);
  int num_bits=2;
  int mask=b>>2;
  int result=a;
  if(b==0)
	return 1;
  if(b==1)
	return a;
  if(a==0)
	return 0;
  if(a==1)
	return 1;
  while(mask) {
	num_bits+=1;
	mask>>=1;
  }
  mask = 1 << (num_bits-2);
  int i=0;
  for(;i<(num_bits-1);i++) {
	if(mask & b)
	  result=result*result*a;
	else
	  result=result*result;
	mask>>=1;
  }
  return result;
}

long rope_hash(PyRopeObject* rope)
{
  long  retval=0;
  if(rope->n_type==ROPE_LITERAL_NODE)
	{
	  int i=0;
	  PY_LONG_LONG retval_hash=0;
	  for(;i<rope->n_length;i++)
		{
		  retval_hash = (1000003*retval_hash) + rope->n_node.literal.l_literal[i];
		  printf("%ld\n", retval_hash);
		}
	  retval=(long) retval_hash;
	  printf("before:%ld\n", retval);
	  retval = retval|0x8000000000000000;
	  printf("after:%ld\n", retval);
	  rope->n_hash=retval;
	}
  else
	{
	  printf("masked_power: %d\n",masked_power(1000003, (rope->n_node.concat.right?rope->n_node.concat.right->n_length:0)));
	  long x = rope_hash(rope->n_node.concat.left) + rope_hash(rope->n_node.concat.right) * masked_power(1000003, (rope->n_node.concat.right?rope->n_node.concat.right->n_length:0));
	  printf("%d\n",x);
	  x = x|0x8000000000000000;
	  retval=x;
	}
  return retval;
}
