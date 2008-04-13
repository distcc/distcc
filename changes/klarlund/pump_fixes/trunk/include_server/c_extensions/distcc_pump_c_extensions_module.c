/* Copyright 2007 Google Inc.
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301,
 * USA.
*/

// Author: Nils Klarlund

/* distcc_pump_c_extensions_module.c -- Python bindings for distcc-pump
 * extensions */

#include "Python.h"

static char *version = ".01";

/* To suppress compiler warnings */
#define UNUSED(v) ((void)&v)

char *rs_program_name = "distcc_include_server";

#include "distcc.h"
#include "rpc.h"

static PyObject *distcc_pump_c_extensionsError;


/***********************************************************************
CompressFileLZO1Z
 ***********************************************************************/

static char CompressLzo1xAlloc_doc__[] =
"CompressFileLZO1Z__(in_buf):\n"
"Compress file according to distcc lzo protocol.\n"
"\n"
"   Arguments:\n"
"     in_buf: a string\n"
"   Raises:\n"
"     distcc_pump_c_extensions.Error\n"
"   Returns:\n"
    " a string, compressed according to distcc protocol\n.";

static PyObject *
CompressLzo1xAlloc(PyObject *dummy, PyObject *args) {
  PyObject *string_object;
  const char *in_buf;
  int in_len;
  char *out_buf;
  size_t out_len;
  UNUSED(dummy);
  if (!PyArg_ParseTuple(args, "s#", &in_buf, &in_len))
    return NULL;
  if (in_len < 0)
    return NULL;
  if (dcc_compress_lzo1x_alloc(in_buf, in_len, &out_buf, &out_len)) {
    PyErr_SetString(distcc_pump_c_extensionsError, 
                    "Couldn't compress that.");
    return NULL;
  }
  string_object = PyString_FromStringAndSize(out_buf, out_len);
  free(out_buf);
  return string_object;
}



/***********************************************************************
Token protocol
************************************************************************/

static char RCwd_doc__[] =
"Rcwd_doc__(ifd):\n"
"   Read value of current directory.\n"
"\n"
"   Arguments:\n"
"     ifd: an integer file descriptor\n"
"   Raises:\n"
"     distcc_pump_c_extensions.Error\n"
;
static PyObject *
RCwd(PyObject *dummy, PyObject *args) {
  int ifd;
  char *value_str;
  UNUSED(dummy);
  if (!PyArg_ParseTuple(args, "i", &ifd))
    return NULL;
  if (dcc_r_cwd(ifd, &value_str)) {
    PyErr_SetString(distcc_pump_c_extensionsError,
                    "Couldn't read token string.");
    return NULL;
  }
  return PyString_FromString(value_str);
}


static char RTokenString_doc__[] =
"RTokenString(ifd, expect_token):\n"
"   Read value of expected token.\n"
"\n"
"   Arguments:\n"
"     ifd: an integer file descriptor\n"
"     expect_token: a four-character string\n"
"   Raises:\n"
"     distcc_pump_c_extensions.Error\n"
;
static PyObject *
RTokenString(PyObject *dummy, PyObject *args) {
  int ifd;
  char *expect_token;
  char *value_str;
  UNUSED(dummy);
  if (!PyArg_ParseTuple(args, "is", &ifd, &expect_token))
    return NULL;
  if (dcc_r_token_string(ifd, expect_token, &value_str)) {
    PyErr_SetString(distcc_pump_c_extensionsError, 
                    "Couldn't read token string.");
    return NULL;
  }
  return PyString_FromString(value_str);
}


static char RArgv_doc__[] =
"Rargv(ifd):\n"
"   Read argv values.\n"
"\n"
"   Arguments:\n"
"     ifd: an integer file descriptor\n"
"   Raises:\n"
"     distcc_pump_c_extensions.Error\n"

;
static PyObject *
RArgv(PyObject *dummy, PyObject *args) {
  int i = 0;
  PyObject *list_object = NULL;
  int ifd;
  PyObject *string_object = NULL;
  char **argv;
  UNUSED(dummy);
  if (!PyArg_ParseTuple(args, "i", &ifd))
    return NULL;
  if (dcc_r_argv(ifd, &argv)) {
    PyErr_SetString(distcc_pump_c_extensionsError, "Couldn't read that.");
    goto error;
  }
  if ((list_object = PyList_New(0)) == NULL) goto error;
  for (; argv[i]; i++) {
    string_object = PyString_FromString(argv[i]);
    free(argv[i]);
    if (!string_object) {
      goto error;
    }
    if (PyList_Append(list_object, string_object) < 0)
      goto error;
    Py_XDECREF(string_object);
  }
  free(argv);
  return list_object;
 error:
  Py_XDECREF(list_object);
  Py_XDECREF(string_object);
  for (i = i + 1; argv[i]; i++) 
    free(argv[i]);
  free(argv);
  return NULL;
}


static char XArgv_doc__[] =
"XArgv(ifd, argv)\n"
"  Transmit list argv.\n"
"\n"
"  Arguments:\n"
"    ifd: integer file descriptor\n"
"    argv: a list of strings\n"
;

static PyObject *
XArgv(PyObject *dummy, PyObject *args) {
  int i;
  char **ptr;
  PyObject *list_object;
  int ifd;
  int len;
  int ret;
  char **argv;
  UNUSED(dummy);
  if (!PyArg_ParseTuple(args, "iO!", &ifd, &PyList_Type, &list_object))
    return NULL;
  len = PyList_Size(list_object);
  argv = ptr = (char **) calloc((size_t) len + 1, sizeof (char *));
  if (ptr == NULL) {
    return PyErr_NoMemory();
  }
  argv[len] = NULL;
  for (i = 0; i < len; i++) {
    PyObject *string_object;
    string_object = PyList_GetItem(list_object, i); /* borrowed ref */
    argv[i] = PyString_AsString(string_object); /* does not increase
						   ref count */
  }
  ret = dcc_x_argv(ifd, argv);
  free(argv);
  if (ret == 0)
    Py_RETURN_TRUE;
  else
    Py_RETURN_FALSE;
}



/***********************************************************************
OsPathExists
************************************************************************/

static /* const */ char OsPathExists_doc__[] =
"OsPathExists(filepath):\n"
"  Libc version of os.path.exists.\n"
"\n"
"  Arguments:\n"
"    filepath: a string\n"
"  Returns:\n"
"    True or False\n"      
;

static PyObject *
OsPathExists(PyObject *dummy, PyObject *args) {
  const char *in;
  int len;
  int res;
  
  struct stat buf;
  
  UNUSED(dummy);
  if (!PyArg_ParseTuple(args, "s#", &in, &len))
    return NULL;
  if (len < 0)
    return NULL;
  res = stat(in, &buf); 
  if (res == -1) Py_RETURN_FALSE;
  if (res == 0) Py_RETURN_TRUE;
  assert(0);
  return NULL;
} 

/***********************************************************************
OsPathIsFile
************************************************************************/

static /* const */ char OsPathIsFile_doc__[] =
"OsPathIsFile(filename):\n"
"  Libc version of os.path.isfile.\n"
"\n"
"  Arguments:\n"
"    filename: a string\n"
"  Returns:\n"
"    True or False\n"      
;

static PyObject *
OsPathIsFile(PyObject *dummy, PyObject *args) {
  const char *in;
  int len;
  int res;
  
  struct stat buf;
  
  UNUSED(dummy);
  if (!PyArg_ParseTuple(args, "s#", &in, &len))
    return NULL;
  if (len < 0)
    return NULL;
  res = stat(in, &buf); 
  if (res == -1) Py_RETURN_FALSE;
  if ((res == 0) && S_ISREG(buf.st_mode)) Py_RETURN_TRUE;
  if ((res == 0) && !S_ISREG(buf.st_mode)) Py_RETURN_FALSE;
  return NULL;
} 



/***********************************************************************
Realpath
***********************************************************************/

static /* const */ char Realpath_doc__[] =
"Realpath(filename)\n"
"  Libc version of os.path.realpath.\n"
"\n"
"  Arguments:\n"
"    filename: a string\n"
"  Returns:\n"
"    the realpath (or filename if it does not exist)\n"
"  The semantics of this function is probably not quite the same as that\n"
"  of os.path.realpath for paths that do not correspond to existing files.\n"
"  This is why we do not call it OsPathRealpath.\n"
"";

/* TODO(klarlund): make logic so that this file will compile in the
   absence of realpath from libc. In that case, use Python realpath. */

static PyObject *
Realpath(PyObject *dummy, PyObject *args) {
  const char *in;
  char *res;
  PyObject *result_str;
  
  UNUSED(dummy);
  if (!PyArg_ParseTuple(args, "s", &in))
    return NULL;
  res = realpath(in, NULL);
  if (res) {
    result_str = PyString_FromStringAndSize(res, strlen(res));
    if (result_str == NULL)
      return PyErr_NoMemory();
    return result_str;
  } 
  else 
    return PyString_FromStringAndSize(in, strlen(in));
} 



/***********************************************************************
Bindings
************************************************************************/

static PyMethodDef methods[] = {
  {"OsPathExists",  (PyCFunction)OsPathExists, METH_VARARGS, 
   OsPathExists_doc__},
  {"OsPathIsFile",  (PyCFunction)OsPathIsFile, METH_VARARGS, 
   OsPathIsFile_doc__},
  {"Realpath",    (PyCFunction)Realpath, METH_VARARGS, Realpath_doc__},
  {"RTokenString",(PyCFunction)RTokenString,   METH_VARARGS, 
   RTokenString_doc__},
  {"RCwd",        (PyCFunction)RCwd,    METH_VARARGS, RCwd_doc__},
  {"RArgv",       (PyCFunction)RArgv,   METH_VARARGS, RArgv_doc__},
  {"XArgv",       (PyCFunction)XArgv,   METH_VARARGS, XArgv_doc__},
  {"CompressLzo1xAlloc", (PyCFunction)CompressLzo1xAlloc, METH_VARARGS, 
   CompressLzo1xAlloc_doc__},
  {NULL, NULL, 0, NULL}
};


static /* const */ char module_documentation[]=
"Various utilities for distcc-pump.\n"
;

void initdistcc_pump_c_extensions(void) {
  PyObject *module;
  PyObject *py_str;
  distcc_pump_c_extensionsError = PyErr_NewException(
      "distcc_pump_c_extensions.Error", NULL, NULL);
  
  module = Py_InitModule4("distcc_pump_c_extensions", 
                          methods, 
                          module_documentation,
                          NULL, 
                          PYTHON_API_VERSION);
  
  py_str = PyString_FromString("Nils Klarlund");
  PyModule_AddObject(module, "__author__", py_str);
  py_str = PyString_FromString(version);
  PyModule_AddObject(module, "__version__", py_str);
  /* Make the exception class accessible */
  PyModule_AddObject(module, "Error",
                     distcc_pump_c_extensionsError);

}
