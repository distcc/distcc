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

#define PY_SSIZE_T_CLEAN
#include "Python.h"

static const char *version = ".01";

/* To suppress compiler warnings */
#define UNUSED(v) ((void)&v)

const char *rs_program_name = "distcc_include_server";

#include "distcc.h"
#include "rpc.h"

static PyObject *distcc_pump_c_extensionsError;
void initdistcc_pump_c_extensions(void);


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
  Py_ssize_t in_len;
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
  string_object = PyBytes_FromStringAndSize(out_buf, out_len);
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
  return PyUnicode_FromString(value_str);
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

  return PyUnicode_FromString(value_str);
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
  if (dcc_r_argv(ifd, "ARGC", "ARGV", &argv)) {
    PyErr_SetString(distcc_pump_c_extensionsError, "Couldn't read that.");
    goto error;
  }
  if ((list_object = PyList_New(0)) == NULL) goto error;
  for (; argv[i]; i++) {
    string_object = PyUnicode_FromString(argv[i]);
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
/* TODO do it properly, catch exceptions for fancy Unicode symbols */
    argv[i] = (char*)PyUnicode_AsUTF8(string_object); /* does not increase
                                                     ref count */
  }
  ret = dcc_x_argv(ifd, "ARGC", "ARGV", argv);
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
  Py_ssize_t len;
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
  Py_ssize_t len;
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

  /* We explicitly allocate memory for the output 'resolved' of 'realpath' --
     otherwise, some systems will make trouble because they do not accept
     passing the second argument NULL (as GNU does) for automatic buffer
     allocation. The glib function 'realpath' comes with the warning to not use
     it because it's difficult to predict the size of the output.  We need the
     function, however, in its C version --- it's much, much faster than the
     Python implementation. (We measured about a factor 20 under Linux.) */
# if defined (PATH_MAX)
#   define DISTCC_PUMP_PATH_MAX PATH_MAX
# elif defined (MAXPATHLEN)
#   define DISTCC_PUMP_PATH_MAX MAXPATHLEN
# else
#   define DISTCC_PUMP_PATH_MAX 4096  // seems conservative for max filename len!
# endif
  char resolved[DISTCC_PUMP_PATH_MAX];
# undef DISTCC_PUMP_PATH_MAX

  char *res;
  PyObject *result_str;

  UNUSED(dummy);
  if (!PyArg_ParseTuple(args, "s", &in))
    return NULL;

  res = realpath(in, resolved);
  if (res) {
    /* On Solaris this result may be a relative path, if the argument was
       relative. Fail hard if this happens. */
    assert(res[0] == '/');
    result_str = PyUnicode_FromStringAndSize(res, strlen(res));
    if (result_str == NULL)
      return PyErr_NoMemory();
    return result_str;
  }
  else {
    return PyUnicode_FromStringAndSize(in, strlen(in));
  }
}



/***********************************************************************
Bindings;
************************************************************************/

struct module_state {
    PyObject *error;
};

#define GETSTATE(m) ((struct module_state*)PyModule_GetState(m))

static const char module_documentation[]=
"Various utilities for distcc-pump.\n"
;

static int module_traverse(PyObject *m, visitproc visit, void *arg) {
    Py_VISIT(GETSTATE(m)->error);
    return 0;
}

static int module_clear(PyObject *m) {
    Py_CLEAR(GETSTATE(m)->error);
    return 0;
}

static PyMethodDef module_methods[] = {
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

static struct PyModuleDef moduledef = {
    PyModuleDef_HEAD_INIT,
    "distcc_pump_c_extensions", /* m_name */
    module_documentation,       /* m_doc */
    sizeof(struct module_state),/* m_size */
    module_methods,             /* m_methods */
    NULL,                       /* m_reload */
    module_traverse,            /* m_traverse */
    module_clear,               /* m_clear */
    NULL,                       /* m_free */
};

PyObject *
PyInit_distcc_pump_c_extensions(void) {
  PyObject *module = PyModule_Create(&moduledef);
  PyObject *py_str;
  distcc_pump_c_extensionsError = PyErr_NewException(
      (char *)"distcc_pump_c_extensions.Error", NULL, NULL);

  if (module == NULL)
    return NULL;

  struct module_state *st = GETSTATE(module);
  st->error = distcc_pump_c_extensionsError;
  if (st->error == NULL) {
    Py_DECREF(module);
    return NULL;
  }

  py_str = PyUnicode_FromString("Nils Klarlund");
  py_str = PyUnicode_FromString(version);
  PyModule_AddObject(module, "__author__", py_str);
  PyModule_AddObject(module, "__version__", py_str);
  /* Make the exception class accessible */
  PyModule_AddObject(module, "Error",
                     distcc_pump_c_extensionsError);
  return module;
}
