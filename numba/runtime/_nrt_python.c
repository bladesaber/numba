#include "_pymodule.h"
#include "nrt.h"

#define NPY_NO_DEPRECATED_API NPY_1_7_API_VERSION
#include <numpy/ndarrayobject.h>
#include <numpy/arrayscalars.h>

/* For Numpy 1.6 */
#ifndef NPY_ARRAY_BEHAVED
    #define NPY_ARRAY_BEHAVED NPY_BEHAVED
#endif


static
PyObject*
memsys_set_atomic_inc_dec(PyObject *self, PyObject *args) {
    unsigned PY_LONG_LONG addr_inc, addr_dec;
    if (!PyArg_ParseTuple(args, "KK", &addr_inc, &addr_dec)) {
        return NULL;
    }
    NRT_MemSys_set_atomic_inc_dec((void*)addr_inc, (void*)addr_dec);
    Py_RETURN_NONE;
}

static
PyObject*
memsys_process_defer_dtor(PyObject *self, PyObject *args) {
    if (!PyArg_ParseTuple(args, "")) {
        return NULL;
    }
    NRT_MemSys_process_defer_dtor();
    Py_RETURN_NONE;
}


static
void pyobject_dtor(void *ptr, void* info) {
    PyGILState_STATE gstate;
    PyObject *ownerobj = info;

    gstate = PyGILState_Ensure();   /* ensure the GIL */
    Py_DECREF(ownerobj);            /* release the python object */
    PyGILState_Release(gstate);     /* release the GIL */
}


static
MemInfo* meminfo_new_from_pyobject(void *data, PyObject *ownerobj) {
    size_t dummy_size = 0;
    Py_INCREF(ownerobj);
    return NRT_MemInfo_new(data, dummy_size, pyobject_dtor, ownerobj);
}


/*
 * Create a new MemInfo with a owner PyObject
 */
static
PyObject*
meminfo_new(PyObject *self, PyObject *args) {
    unsigned PY_LONG_LONG addr_data;
    PyObject* ownerobj;
    MemInfo *mi;
    if (!PyArg_ParseTuple(args, "KO", &addr_data, &ownerobj)) {
        return NULL;
    }
    mi = meminfo_new_from_pyobject((void*)addr_data, ownerobj);
    return Py_BuildValue("K", mi);
}

/*
 * Create a new MemInfo with a new NRT allocation
 */
static
PyObject*
meminfo_alloc(PyObject *self, PyObject *args) {
    MemInfo *mi;
    Py_ssize_t size;
    if (!PyArg_ParseTuple(args, "K", &size)) {
        return NULL;
    }
    mi = NRT_MemInfo_alloc(size);
    return Py_BuildValue("K", mi);
}

/*
 * Like meminfo_alloc but set memory to zero after allocation and before
 * deallocation.
 */
static
PyObject*
meminfo_alloc_safe(PyObject *self, PyObject *args) {
    MemInfo *mi;
    Py_ssize_t size;
    if (!PyArg_ParseTuple(args, "K", &size)) {
        return NULL;
    }
    mi = NRT_MemInfo_alloc_safe(size);
    return Py_BuildValue("K", mi);
}

typedef struct {
    PyObject_HEAD
    MemInfo *meminfo;
    int      defer;
} MemInfoObject;

static
int MemInfo_init(MemInfoObject *self, PyObject *args, PyObject *kwds) {
    static char *keywords[] = {"ptr", NULL};
    unsigned PY_LONG_LONG raw_ptr;
    if (!PyArg_ParseTupleAndKeywords(args, kwds, "K", keywords, &raw_ptr)) {
        return -1;
    }
    self->meminfo = (MemInfo*)raw_ptr;
    self->defer = 0;
    NRT_MemInfo_acquire(self->meminfo);
    return 0;
}

int MemInfo_getbuffer(PyObject *exporter, Py_buffer *view, int flags) {
    Py_ssize_t len;
    void *buf;
    int readonly = 0;

    MemInfoObject *miobj = (MemInfoObject*)exporter;
    MemInfo *mi = miobj->meminfo;

    buf = NRT_MemInfo_data(mi);
    len = NRT_MemInfo_size(mi);
    return PyBuffer_FillInfo(view, exporter, buf, len, readonly, flags);
}

Py_ssize_t MemInfo_rdwrbufferproc(PyObject *self, Py_ssize_t segment,
                                  void **ptrptr)
{
    MemInfoObject *mio = (MemInfoObject *)self;
    MemInfo *mi = mio->meminfo;
    if (segment != 0) {
        PyErr_SetString(PyExc_TypeError, "MemInfo only has 1 segment");
        return -1;
    }
    *ptrptr = NRT_MemInfo_data(mi);
    return NRT_MemInfo_size(mi);
}

Py_ssize_t MemInfo_segcountproc(PyObject *self, Py_ssize_t *lenp) {
    MemInfoObject *mio = (MemInfoObject *)self;
    MemInfo *mi = mio->meminfo;
    if (lenp) {
        *lenp = NRT_MemInfo_size(mi);
    }
    return 1;
}

#if (PY_MAJOR_VERSION < 3)
static PyBufferProcs MemInfo_bufferProcs = {MemInfo_rdwrbufferproc,
                                            MemInfo_rdwrbufferproc,
                                            MemInfo_segcountproc,
                                            NULL};
#else
static PyBufferProcs MemInfo_bufferProcs = {MemInfo_getbuffer, NULL};
#endif

static
PyObject*
MemInfo_acquire(MemInfoObject *self) {
    NRT_MemInfo_acquire(self->meminfo);
    Py_RETURN_NONE;
}

static
PyObject*
MemInfo_release(MemInfoObject *self) {
    NRT_MemInfo_release(self->meminfo, self->defer);
    Py_RETURN_NONE;
}

static
PyObject*
MemInfo_release_defer(MemInfoObject *self) {
    NRT_MemInfo_release(self->meminfo, 1);
    Py_RETURN_NONE;
}

static
PyObject*
MemInfo_set_defer(MemInfoObject *self, PyObject *args) {
    PyObject *should_defer;
    int defer;
    if (!PyArg_ParseTuple(args, "O", &should_defer)) {
        return NULL;
    }
    defer = PyObject_IsTrue(should_defer);
    if (defer == -1) {
        return NULL;
    }
    self->defer = defer;
    Py_RETURN_NONE;
}


static
PyObject*
MemInfo_get_defer(MemInfoObject *self) {
    if (self->defer) {
        Py_RETURN_TRUE;
    } else {
        Py_RETURN_FALSE;
    }
}


static
PyObject*
MemInfo_get_data(MemInfoObject *self) {
    return Py_BuildValue("K", NRT_MemInfo_data(self->meminfo));
}

static void
MemInfo_dealloc(MemInfoObject *self)
{
    NRT_MemInfo_release(self->meminfo, self->defer);
    Py_TYPE(self)->tp_free((PyObject*)self);
}

static PyMethodDef MemInfo_methods[] = {
    {"acquire", (PyCFunction)MemInfo_acquire, METH_NOARGS,
     "Increment the reference count"
    },
    {"release", (PyCFunction)MemInfo_release, METH_NOARGS,
     "Decrement the reference count"
    },
    {"release_defer", (PyCFunction)MemInfo_release_defer, METH_NOARGS,
     "Decrement the reference count but defer the destructor regardless"
     "of the value in defer attribute"
    },
    {"set_defer", (PyCFunction)MemInfo_set_defer, METH_VARARGS,
     "Set the defer attribute"
    },
    {"get_defer", (PyCFunction)MemInfo_get_defer, METH_NOARGS,
     "Get the defer attribute"
    },
    {"get_data", (PyCFunction)MemInfo_get_data, METH_NOARGS,
     "Get the data pointer as an integer"
    },
    {NULL}  /* Sentinel */
};



static PyTypeObject MemInfoType = {
#if (PY_MAJOR_VERSION < 3)
    PyObject_HEAD_INIT(NULL)
    0,                                        /* ob_size*/
#else
    PyVarObject_HEAD_INIT(NULL, 0)
#endif
    "_nrt_python._MemInfo",                   /* tp_name*/
    sizeof(MemInfoObject),                    /* tp_basicsize*/
    0,                                        /* tp_itemsize*/
    (destructor)MemInfo_dealloc,              /* tp_dealloc*/
    0,                                        /* tp_print*/
    0,                                        /* tp_getattr*/
    0,                                        /* tp_setattr*/
    0,                                        /* tp_compare*/
    0,                                        /* tp_repr*/
    0,                                        /* tp_as_number*/
    0,                                        /* tp_as_sequence*/
    0,                                        /* tp_as_mapping*/
    0,                                        /* tp_hash */
    0,                                        /* tp_call*/
    0,                                        /* tp_str*/
    0,                                        /* tp_getattro*/
    0,                                        /* tp_setattro*/
    &MemInfo_bufferProcs,                      /* tp_as_buffer*/
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,  /* tp_flags*/
    0,                                        /* tp_doc */
    0,                                        /* tp_traverse */
    0,                                        /* tp_clear */
    0,                                        /* tp_richcompare */
    0,                                        /* tp_weaklistoffset */
    0,                                        /* tp_iter */
    0,                                        /* tp_iternext */
    MemInfo_methods,                          /* tp_methods */
    0,                                        /* tp_members */
    0,                                        /* tp_getset */
    0,                                        /* tp_base */
    0,                                        /* tp_dict */
    0,                                        /* tp_descr_get */
    0,                                        /* tp_descr_set */
    0,                                        /* tp_dictoffset */
    (initproc)MemInfo_init,                   /* tp_init */
    0,                                        /* tp_alloc */
    0,                                        /* tp_new */
};


/****** Array adaptor code ******/


/*
 * Fill in the *arystruct* with information from the Numpy array *obj*.
 * *arystruct*'s layout is defined in numba.targets.arrayobj (look
 * for the ArrayTemplate class).
 */

typedef struct {
    void     *meminfo;
    PyObject *parent;
    npy_intp nitems;
    npy_intp itemsize;
    void *data;

    npy_intp shape_and_strides[];
} arystruct_t;

static
int NRT_adapt_ndarray(PyObject *obj, arystruct_t* arystruct) {
    PyArrayObject *ndary;
    int i, ndim;
    npy_intp *p;
    void *data;

    if (!PyArray_Check(obj)) {
        return -1;
    }

    ndary = (PyArrayObject*)obj;
    ndim = PyArray_NDIM(ndary);
    data = PyArray_DATA(ndary);

    arystruct->meminfo = meminfo_new_from_pyobject((void*)data, obj);
    arystruct->data = data;
    arystruct->nitems = PyArray_SIZE(ndary);
    arystruct->itemsize = PyArray_ITEMSIZE(ndary);
    arystruct->parent = obj;
    p = arystruct->shape_and_strides;
    for (i = 0; i < ndim; i++, p++) {
        *p = PyArray_DIM(ndary, i);
    }
    for (i = 0; i < ndim; i++, p++) {
        *p = PyArray_STRIDE(ndary, i);
    }

    NRT_Debug(nrt_debug_print("NRT_adapt_ndarray %p\n", arystruct->meminfo));

    NRT_MemInfo_acquire(arystruct->meminfo);
    return 0;
}


static
PyObject* NRT_adapt_native_array(arystruct_t* arystruct, int ndim,
                                 int type_num) {
    PyObject *array;
    MemInfoObject *miobj;
    PyObject *args;
    npy_intp *shape, *strides;
    int flags=NPY_ARRAY_WRITEABLE;

    miobj = PyObject_New(MemInfoObject, &MemInfoType);
    PyObject_Init((PyObject*)miobj, &MemInfoType);
    args = Py_BuildValue("(K)", (unsigned PY_LONG_LONG)arystruct->meminfo);
    if(MemInfo_init(miobj, args, NULL)) {
        return NULL;
    }
    Py_DECREF(args);

    shape = arystruct->shape_and_strides;
    strides = shape + ndim;
    array = PyArray_New(&PyArray_Type, ndim, shape, type_num,
                        strides, NRT_MemInfo_data(arystruct->meminfo),
                        arystruct->itemsize, flags, (PyObject*)miobj);
    /* Set the MemInfoObject as the base object */
    PyArray_SetBaseObject((PyArrayObject*)array, (PyObject *)miobj);
    return array;
}

static void
NRT_adapt_buffer(Py_buffer *buf, arystruct_t *arystruct)
{
    int i;
    npy_intp *p;

    arystruct->meminfo = NULL; /* meminfo_new_from_pyobject((void*)buf->buf, buf->obj); */
    arystruct->data = buf->buf;
    arystruct->itemsize = buf->itemsize;
    arystruct->parent = buf->obj;
    arystruct->nitems = 1;
    p = arystruct->shape_and_strides;
    for (i = 0; i < buf->ndim; i++, p++) {
        *p = buf->shape[i];
        arystruct->nitems *= buf->shape[i];
    }
    for (i = 0; i < buf->ndim; i++, p++) {
        *p = buf->strides[i];
    }

    /* NRT_MemInfo_acquire(arystruct->meminfo); */
}

static void
NRT_incref(MemInfo* mi) {
    if (mi) {
        NRT_MemInfo_acquire(mi);
    }
}

static void
NRT_decref(MemInfo* mi) {
    if (mi) {
        NRT_MemInfo_release(mi, 0);
    }
}

static PyMethodDef ext_methods[] = {
#define declmethod(func) { #func , ( PyCFunction )func , METH_VARARGS , NULL }
    declmethod(memsys_set_atomic_inc_dec),
    declmethod(memsys_process_defer_dtor),
    declmethod(meminfo_new),
    declmethod(meminfo_alloc),
    declmethod(meminfo_alloc_safe),
    { NULL },
#undef declmethod
};



static PyObject *
build_c_helpers_dict(void)
{
    PyObject *dct = PyDict_New();
    if (dct == NULL)
        goto error;

#define _declpointer(name, value) do {                 \
    PyObject *o = PyLong_FromVoidPtr(value);           \
    if (o == NULL) goto error;                         \
    if (PyDict_SetItemString(dct, name, o)) {          \
        Py_DECREF(o);                                  \
        goto error;                                    \
    }                                                  \
    Py_DECREF(o);                                      \
} while (0)

#define declmethod(func) _declpointer(#func, &NRT_##func)

declmethod(adapt_ndarray);
declmethod(adapt_native_array);
declmethod(adapt_buffer);
declmethod(incref);
declmethod(decref);
declmethod(MemInfo_data);
declmethod(MemInfo_alloc);


#undef declmethod
    return dct;
error:
    Py_XDECREF(dct);
    return NULL;
}

MOD_INIT(_nrt_python) {
    PyObject *m;
    MOD_DEF(m, "_nrt_python", "No docs", ext_methods)
    if (m == NULL)
        return MOD_ERROR_VAL;
    import_array();

    MemInfoType.tp_new = PyType_GenericNew;
    if (PyType_Ready(&MemInfoType))
        return MOD_ERROR_VAL;

    Py_INCREF(&MemInfoType);
    PyModule_AddObject(m, "_MemInfo", (PyObject *) (&MemInfoType));

    PyModule_AddObject(m, "c_helpers", build_c_helpers_dict());

    return MOD_SUCCESS_VAL(m);
}
