#include "Channel.h"

#include "Tasklet.h"

#include "PyChannel.h"

#include <new>

static PyObject*
	Channel_new( PyTypeObject* type, PyObject* args, PyObject* kwds )
{
	PyChannelObject* self;

	self = (PyChannelObject*)type->tp_alloc( type, 0 );

	if( self != nullptr )
	{
		self->m_impl = nullptr;

		self->m_weakref_list = nullptr;
	}

	return (PyObject*)self;
}

static int
	Channel_init( PyChannelObject* self, PyObject* Py_UNUSED( args ), PyObject* Py_UNUSED( kwds ) )
{

	// Allocate the memory for the implementation member
	self->m_impl = (Channel*)PyObject_Malloc( sizeof( Channel ) );

	if( !self->m_impl )
	{
		PyErr_SetString( PyExc_RuntimeError, "Failed to allocate memory for implementation object." );

		return -1;
	}

    // Call constructor
	try
	{
		new( self->m_impl ) Channel( reinterpret_cast<PyObject*>( self ) );
	}
	catch( const std::exception& ex )
	{
		PyObject_Free( self->m_impl );

		PyErr_SetString( PyExc_RuntimeError, ex.what() );

		return -1;
	}
	catch( ... )
	{
		PyObject_Free( self->m_impl );

		PyErr_SetString( PyExc_RuntimeError, "Failed to construct implementation object." );

		return -1;
	}

	return 0;
}

static void
	Channel_dealloc( PyChannelObject* self )
{
    if (self->m_impl)
    {
		// Call destructor
		self->m_impl->~Channel();

		PyObject_Free( self->m_impl );
    }

    // Handle weakrefs
    if (self->m_weakref_list != nullptr)
    {
		PyObject_ClearWeakRefs( (PyObject*)self );
    }

    Py_TYPE( self )->tp_free( (PyObject*)self );
}

static bool PyChannelObject_is_valid( PyChannelObject* channel )
{
	if( !channel->m_impl )
	{
		PyErr_SetString( PyExc_RuntimeError, "Channel object is not valid. Most likely cause being __init__ not called on base type." );

		return false;
	}

	return true;
}

static PyObject*
	Channel_preference_get( PyChannelObject* self, void* closure )
{
	// Ensure PyChannelObject is in a valid state
	if( !PyChannelObject_is_valid( self ) )
	{
		return nullptr;
	}

	return PyLong_FromLong( self->m_impl->preference() );
}

static int
	Channel_preference_set( PyChannelObject* self, PyObject* value, void* closure ) //TODO just test
{
	// Ensure PyChannelObject is in a valid state
	if( !PyChannelObject_is_valid( self ) )
	{
		return -1;
	}

	if( value == NULL )
	{
		PyErr_SetString( PyExc_TypeError, "Cannot delete the first attribute" );
		return -1;
	}
	if( !PyLong_Check( value ) )
	{
		PyErr_SetString( PyExc_TypeError,
						 "The first attribute value must be a number" );
		return -1;
	}

    long new_preference = PyLong_AsLong( value );

    // Only accept valid values
    // -1   - Prefer receive
    // 0    - Prefer neither
    // 1    - Prefer sender
    if( ( new_preference > -2 ) && ( new_preference < 2 ) )
	{
		self->m_impl->set_preference( new_preference );
    }

	return 0;
}

static PyObject*
	Channel_balance_get( PyChannelObject* self, void* closure )
{
	// Ensure PyChannelObject is in a valid state
	if( !PyChannelObject_is_valid( self ) )
	{
		return nullptr;
	}

	return PyLong_FromLong( self->m_impl->balance() );
}

static PyObject*
	Channel_queue_get( PyChannelObject* self, void* closure )
{
	// Ensure PyChannelObject is in a valid state
	if( !PyChannelObject_is_valid( self ) )
	{
		return nullptr;
	}

	Tasklet* front = self->m_impl->blocked_queue_front();

    if (!front)
    {
		Py_IncRef(Py_None);

		return Py_None;
    }
    else
    {
		PyObject* front_of_queue = front->python_object();

        Py_IncRef( front_of_queue );

		return front_of_queue;
    }
}

static PyObject*
	Channel_closed_get( PyChannelObject* self, void* closure )
{
	// Ensure PyChannelObject is in a valid state
	if( !PyChannelObject_is_valid( self ) )
	{
		return nullptr;
	}

	return self->m_impl->is_closed() ? Py_True : Py_False;
}

static PyObject*
	Channel_closing_get( PyChannelObject* self, void* closure )
{
	// Ensure PyChannelObject is in a valid state
	if( !PyChannelObject_is_valid( self ) )
	{
		return nullptr;
	}

	return self->m_impl->is_closing() ? Py_True : Py_False ;
}

static PyGetSetDef Channel_getsetters[] = {
	{ "preference",
        (getter)Channel_preference_get,
        (setter)Channel_preference_set,
        "allows for customisation of how the channel actions.",
        NULL },

	{ "balance",
        (getter)Channel_balance_get,
        NULL,
        "number of tasklets waiting to send (>0) or receive (<0).",
        NULL },

	{ "queue",
        (getter)Channel_queue_get,
        NULL,
        "the first tasklet in the chain of tasklets that are blocked on the channel.",
        NULL },

	{ "closed",
        (getter)Channel_closed_get,
        NULL,
        "The value of this attribute is True when close() has been called and the channel is empty.",
        NULL },

	{ "closing",
        (getter)Channel_closing_get,
        NULL,
        "The value of this attribute is True when close() has been called.",
        NULL },

	{ NULL } /* Sentinel */
};

static PyObject*
	Channel_send( PyChannelObject* self, PyObject* args, PyObject* Py_UNUSED( kwds ) )
{
	// Ensure PyChannelObject is in a valid state
	if( !PyChannelObject_is_valid( self ) )
	{
		return nullptr;
	}

	PyObject* value;

	if( !PyArg_ParseTuple( args, "O:Channel.send", &value ) )
	{
		return nullptr;
	}

	if( !self->m_impl->send( value ) )
	{
		return nullptr;
	}

    Py_IncRef( Py_None );

	return Py_None;
}

static PyObject*
	Channel_receive( PyChannelObject* self, PyObject* Py_UNUSED( ignored ) )
{
	// Ensure PyChannelObject is in a valid state
	if( !PyChannelObject_is_valid( self ) )
	{
		return nullptr;
	}

	return self->m_impl->receive();
}

static PyObject*
	Channel_sendexception( PyChannelObject* self, PyObject* args, PyObject* Py_UNUSED( kwds ) )
{
	// Ensure PyChannelObject is in a valid state
	if( !PyChannelObject_is_valid( self ) )
	{
		return nullptr;
	}

    if (PyTuple_Size(args) < 1)
    {
		PyErr_SetString( PyExc_RuntimeError, "Exception type required" );
		return nullptr;
    }

    PyObject* exception = PyTuple_GetItem(args,0);

    if( !PyExceptionClass_Check( exception ) && !PyObject_IsInstance( exception, PyExc_Exception ) )
    {
		PyErr_SetString( PyExc_RuntimeError, "Exception type or instance required" );
		return nullptr;
    }

    Py_IncRef( exception );

    PyObject* exception_arguments = nullptr;

    if (PyTuple_Size(args) > 1)
    {
		exception_arguments = PyTuple_GetSlice( args, 1, PyTuple_Size( args ) );
    }
    else
    {
		exception_arguments = PyTuple_New( 0 );
    }

	if( !self->m_impl->send( exception_arguments, exception ) )
	{
		Py_DecRef( exception_arguments );

		return NULL;
    }

    Py_DecRef( exception_arguments );

    Py_IncRef( Py_None );

	return Py_None;
}

static PyObject*
	Channel_sendThrow( PyChannelObject* self, PyObject* args, PyObject* kwds )
{
	// Ensure PyChannelObject is in a valid state
	if( !PyChannelObject_is_valid( self ) )
	{
		return nullptr;
	}

	const char* kwlist[] = { "exc", "val", "tb", NULL };

	PyObject* exception = nullptr;
	PyObject* value = Py_None;
	PyObject* tb = Py_None;

	if( !PyArg_ParseTupleAndKeywords( args, kwds, "O|OO:Channel.send_throw", (char**)kwlist, &exception, &value, &tb ) )
	{
		return nullptr;
	}

    /*
    * We are keeping this check around only to adhere to the previous stackless implementation
    * We only rely on the value here, not exception or the tb (traceback) object
    */
	if( !PyExceptionClass_Check( exception ) && !PyObject_IsInstance( exception, PyExc_Exception ) )
	{
		PyErr_SetString( PyExc_TypeError, "Channel.send_throw() argument 'exc' (pos 1) must be an Exception type or instance" );
		return nullptr;
	}

	Py_IncRef( value );
	Py_IncRef( tb );
	Py_IncRef( exception );

    Py_INCREF( Py_None );

    auto exceptionDataTuple = PyTuple_New( 3 );
	PyTuple_SetItem( exceptionDataTuple, 0, exception );
	PyTuple_SetItem( exceptionDataTuple, 1, value );
	PyTuple_SetItem( exceptionDataTuple, 2, tb );

    if( !self->m_impl->send( Py_None, exceptionDataTuple ) )
	{
		Py_DecRef( exceptionDataTuple );
		return NULL;
	}
    
    Py_IncRef( Py_None );

	return Py_None;
}


static PyObject*
	Channel_iter( PyChannelObject* self )
{
	Py_INCREF( self ); 

	return reinterpret_cast<PyObject*>(self);
}

static PyObject*
	Channel_next( PyChannelObject* self )
{
    // Run receive until unblocked
    // Note: behaviour is slightly different to stackless but probably better
    // At end of iteration there will be an error due to DEADLOCK
    // This will return a nullptr
    // This null then returned here will turn this into a StopIteration error
    // Which makes more sense
	PyObject* ret = Channel_receive( self, nullptr );

    if (!ret)
    {
		PyErr_SetString( PyExc_StopIteration, "Channel is closed" );    //TODO: This is not technically true, requires Stackless investigation

        return nullptr;
    }
    else
    {
		return ret;
    }

}

static PyObject*
	Channel_clearTasklets( PyChannelObject* self, PyObject* Py_UNUSED( ignored ) )
{
	// Ensure PyChannelObject is in a valid state
	if( !PyChannelObject_is_valid( self ) )
	{
		return nullptr;
	}

	self->m_impl->clear_blocked( false );

	Py_IncRef( Py_None );

    return Py_None;
}

static PyObject*
	Channel_close( PyChannelObject* self, PyObject* Py_UNUSED( ignored ) )
{
	// Ensure PyChannelObject is in a valid state
	if( !PyChannelObject_is_valid( self ) )
	{
		return nullptr;
	}

	self->m_impl->close();

	Py_IncRef( Py_None );

	return Py_None;
}

static PyObject*
	Channel_open( PyChannelObject* self, PyObject* Py_UNUSED( ignored ) )
{
	// Ensure PyChannelObject is in a valid state
	if( !PyChannelObject_is_valid( self ) )
	{
		return nullptr;
	}

	self->m_impl->open();

	Py_IncRef( Py_None );

	return Py_None;
}

static PyMethodDef Channel_methods[] = {
	{ "send",
        (PyCFunction)Channel_send,
        METH_VARARGS,
        "Send an object over the channel. \n\n\
            :param value: Value to send \n\
            :type value: Object" },

	{ "receive",
        (PyCFunction)Channel_receive,
        METH_NOARGS,
        "Receive an object over the channel. \n\n\
            :return received value" },

	{ "send_exception",
        (PyCFunction)Channel_sendexception,
        METH_VARARGS,
        "Send an exception over the channel. \n\n\
            :param exc: Python exception \n\
            :type exc: sub-class of Python exception \n\
            :param args: Arguments to apply to exception \n\
            :type args: Tuple" },

	{ "send_throw",
        (PyCFunction)Channel_sendThrow,
        METH_VARARGS | METH_KEYWORDS,
        "Send an exception over the channel. \n\n\
            :param exc: Python exception \n\
            :type exc: sub-class of Python exception \n\
            :param val: Value to apply to exception \n\
            :type val: Tuple \n\
            :param tb: Traceback \n\
            :type tb: Python Traceback object" },

	{ "clear",
        (PyCFunction)Channel_clearTasklets,
        METH_NOARGS,
        "Clear channel, all blocked tasklets will be killed rasing TaskletExit exception." },

	{ "close",
        (PyCFunction)Channel_close,
        METH_NOARGS,
        "Prevents the channel queue from growing. If the channel is not empty, the flag closing becomes True. If the channel is empty, the flag closed becomes True." },

	{ "open",
        (PyCFunction)Channel_open,
        METH_NOARGS,
        "Reopen a channel." },

	{ NULL } /* Sentinel */
};

static PyTypeObject ChannelType = {
	/* The ob_type field must be initialized in the module init function
     * to be portable to Windows without using C++. */
	PyVarObject_HEAD_INIT( NULL, 0 ) "scheduler.Channel", /*tp_name*/
	sizeof( PyChannelObject ), /*tp_basicsize*/
	0, /*tp_itemsize*/
	/* methods */
	(destructor)Channel_dealloc, /*tp_dealloc*/
	0, /*tp_vectorcall_offset*/
	0, /*tp_getattr*/
	0, /*tp_setattr*/
	0, /*tp_as_async*/
	0, /*tp_repr*/
	0, /*tp_as_number*/
	0, /*tp_as_sequence*/
	0, /*tp_as_mapping*/
	0, /*tp_hash*/
	0, /*tp_call*/
	0, /*tp_str*/
	0, /*tp_getattro*/
	0, /*tp_setattro*/
	0, /*tp_as_buffer*/
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /*tp_flags*/
	PyDoc_STR( "Channel objects" ), /*tp_doc*/
	0, /*tp_traverse*/
	0, /*tp_clear*/
	0, /*tp_richcompare*/
	offsetof( PyChannelObject, m_weakref_list ), /*tp_weaklistoffset*/
	(getiterfunc)Channel_iter, /*tp_iter*/
	(iternextfunc)Channel_next, /*tp_iternext*/
	Channel_methods, /*tp_methods*/
	0, /*tp_members*/
	Channel_getsetters, /*tp_getset*/
	0,
	/* see PyInit_xx */ /*tp_base*/
	0, /*tp_dict*/
	0, /*tp_descr_get*/
	0, /*tp_descr_set*/
	0, /*tp_dictoffset*/
	(initproc)Channel_init, /*tp_init*/
	0, /*tp_alloc*/
	Channel_new, /*tp_new*/
	0, /*tp_free*/
	0, /*tp_is_gc*/
};