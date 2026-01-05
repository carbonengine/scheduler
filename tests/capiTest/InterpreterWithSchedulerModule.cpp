#include "StdAfx.h"

#include "InterpreterWithSchedulerModule.h"

// Include build config specific paths
// #include <PackagePaths.h>
// #include <codecvt>
// #include <locale>
// #include <string>
// #include <iostream>
// #include <cstdint>

#include <StringConversions.h>

static SchedulerCAPI* s_schedulerApi = nullptr;
static int s_testValue = 0;

static PyObject*
	schedulertest_channel_send( PyObject* self, PyObject* args )
{
	PyObject* channel = Py_None;

	PyObject* value = Py_None;

    if( !PyArg_ParseTuple( args, "OO", &channel, &value ) )
	{
		return NULL;
    }

    if (!s_schedulerApi->PyChannel_Check(channel))
    {
		return NULL;
    }

    Py_IncRef( value );

    s_testValue = 1; // Increment test value

    int ret_val = s_schedulerApi->PyChannel_Send( reinterpret_cast<PyChannelObject*>(channel), value );

    s_testValue = ret_val; // Will be -1 if failed (eg tasklet killed)

    Py_DecRef( value );
	
	if( ret_val == 0 )
	{
		Py_IncRef( Py_None );

		return Py_None;
	}
	else
	{
		return nullptr;
	}
}

static PyObject*
	schedulertest_channel_send_throw( PyObject* self, PyObject* args )
{
	PyObject* channel = Py_None;

	PyObject* exception = Py_None;
	PyObject* value = nullptr;
	PyObject* tb = nullptr;

	if( !PyArg_ParseTuple( args, "OO|OO", &channel, &exception, &value, &tb ) )
	{
		return NULL;
	}

	if( !s_schedulerApi->PyChannel_Check( channel ) )
	{
		return NULL;
	}

	Py_IncRef( exception );

    Py_IncRef( value );

    Py_IncRef( tb );

	s_testValue = 1; // Increment test value

	int ret_val = s_schedulerApi->PyChannel_SendThrow( reinterpret_cast<PyChannelObject*>( channel ),exception, value, tb );

	s_testValue = ret_val; // Will be -1 if failed (eg tasklet killed)

    Py_DecRef( exception );

	Py_DecRef( value );

    Py_DecRef( tb );

	if(ret_val == 0)
	{
		Py_IncRef( Py_None );

		return Py_None;
	}
	else
	{
		return nullptr;
	}
}

static PyObject*
	schedulertest_channel_receive( PyObject* self, PyObject* args )
{
	PyObject* channel = Py_None;

	if( !PyArg_ParseTuple( args, "O", &channel ) )
	{
		return NULL;
	}

	if( !s_schedulerApi->PyChannel_Check( channel ) )
	{
		return NULL;
	}

	s_testValue = 1; // Increment test value

	PyObject* ret_val = s_schedulerApi->PyChannel_Receive( reinterpret_cast<PyChannelObject*>( channel ) );

    s_testValue = ret_val ? 0 : -1;

	return ret_val;
}

static PyObject*
	schedulertest_schedule( PyObject* self, PyObject* args )
{
	int remove = -1;

	if( !PyArg_ParseTuple( args, "i", &remove ) )
	{
		return NULL;
	}

    return s_schedulerApi->PyScheduler_Schedule( nullptr, remove );
}

static PyObject*
	schedulertest_test_value( PyObject* self, PyObject* args )
{
	return PyLong_FromLong( s_testValue );
}

static PyObject*
	schedulertest_send_exception( PyObject* self, PyObject* args )
{
	PyObject* channel = Py_None;

	PyObject* klass = nullptr;

    PyObject* value = nullptr;

	if( !PyArg_ParseTuple( args, "OO|O", &channel, &klass, &value ) )
	{
		return NULL;
	}

    if (s_schedulerApi->PyChannel_SendException(reinterpret_cast<PyChannelObject*>(channel), klass, value) == 0)
    {
		Py_IncRef( Py_None );

		return Py_None;
    }
    else
    {
		return NULL;
    }
}

static PyMethodDef SchedulerTestMethods[] = {
	{ "channel_send", schedulertest_channel_send, METH_VARARGS, "sends using c-api PyChannel_Send" },
	{ "channel_send_throw", schedulertest_channel_send_throw, METH_VARARGS, "throws using c-api PyChannel_SendThrow" },
	{ "channel_receive", schedulertest_channel_receive, METH_VARARGS, "receives using c-api PyChannel_Receive" },
	{ "schedule", schedulertest_schedule, METH_VARARGS, "Schedules using c-api PyScheduler_Schedule" },
	{ "send_exception", schedulertest_send_exception, METH_VARARGS, "Sends and exception using c-api PyScheduler_Schedule" },
	{ "test_value", schedulertest_test_value, METH_VARARGS, "Returns the current state of the test value" },
	{ NULL, NULL, 0, NULL }
};

static PyModuleDef SchedulerTestModule = {
	PyModuleDef_HEAD_INIT,
	"schedulertest",
	NULL,
	-1,
	SchedulerTestMethods,
	NULL,
	NULL,
	NULL,
	NULL
};

static PyObject*
	PyInit_schedulertest( void )
{
	return PyModule_Create( &SchedulerTestModule );
}

void InterpreterWithSchedulerModule::SetUp()
{

	PyConfig config;

	PyConfig_InitPythonConfig( &config );

	PyStatus status;

	const char* programName = "SchedulerCapiTest";

	status = PyConfig_SetBytesString( &config, &config.program_name, programName );
	if( PyStatus_Exception( status ) )
	{
		PyErr_Print();
		exit( -1 );
	}

	/* Read all configuration at once */
	status = PyConfig_Read( &config );
	if( PyStatus_Exception( status ) )
	{
		PyErr_Print();
		exit( -1 );
	}

	std::wstring SCHEDULER_CEXTENSION_MODULE_PATH = UTF8ToWide(std::getenv("SCHEDULER_CEXTENSION_MODULE_PATH"));
	std::wstring SCHEDULER_PACKAGE_PATH =			UTF8ToWide(std::getenv("SCHEDULER_PACKAGE_PATH"));
	std::wstring STDLIB_PATH =						UTF8ToWide(std::getenv("STDLIB_PATH"));
	std::wstring GREENLET_CEXTENSION_MODULE_PATH =	UTF8ToWide(std::getenv("GREENLET_CEXTENSION_MODULE_PATH"));
	std::wstring GREENLET_MODULE_PATH =				UTF8ToWide(std::getenv("GREENLET_MODULE_PATH"));

	// Setup search paths
	status = PyWideStringList_Append( &config.module_search_paths, SCHEDULER_CEXTENSION_MODULE_PATH.c_str() );
	if( PyStatus_Exception( status ) )
	{
		PyErr_Print();
		exit( -1 );
	}

	// Setup search paths
	status = PyWideStringList_Append( &config.module_search_paths, SCHEDULER_PACKAGE_PATH.c_str() );
	if( PyStatus_Exception( status ) )
	{
		PyErr_Print();
		exit( -1 );
	}

	status = PyWideStringList_Append( &config.module_search_paths, STDLIB_PATH.c_str() );
	if( PyStatus_Exception( status ) )
	{
		PyErr_Print();
		exit( -1 );
	}

	status = PyWideStringList_Append( &config.module_search_paths, GREENLET_CEXTENSION_MODULE_PATH.c_str() );
	if( PyStatus_Exception( status ) )
	{
		PyErr_Print();
		exit( -1 );
	}

	status = PyWideStringList_Append( &config.module_search_paths, GREENLET_MODULE_PATH.c_str() );
	if( PyStatus_Exception( status ) )
	{
		PyErr_Print();
		exit( -1 );
	}

	// Add to search paths
	config.module_search_paths_set = 1;

	// Allow environment variables
	config.use_environment = 0;

    // Reset test value
    s_testValue = 0;

    // Import extension used in tests
	PyImport_AppendInittab( "schedulertest", &PyInit_schedulertest );

	status = Py_InitializeFromConfig( &config );

	if( PyStatus_Exception( status ) )
	{
		PyErr_Print();
		exit( -1 );
	}
	PyConfig_Clear( &config );

#if _MSC_VER
#define strcasecmp _stricmp
#endif

	const char* buildflavor = std::getenv( "BUILDFLAVOR" );

	if( strcasecmp( buildflavor, "release" ) == 0 )
	{
		m_schedulerModule = PyImport_ImportModule( "_scheduler" );
	}
	else if( strcasecmp( buildflavor, "internal" ) == 0 )
	{
		m_schedulerModule = PyImport_ImportModule( "_scheduler_internal" );
	}
	else if( strcasecmp( buildflavor, "trinitydev" ) == 0 )
	{
		m_schedulerModule = PyImport_ImportModule( "_scheduler_trinitydev" );
	}
	else if( strcasecmp( buildflavor, "debug" ) == 0 )
	{
		m_schedulerModule = PyImport_ImportModule( "_scheduler_debug" );
	}

#if _MSC_VER
#undef strcasecmp
#endif

	auto* sys_modules = PyImport_GetModuleDict();
	if ( PyDict_SetItemString(sys_modules, "_scheduler", m_schedulerModule) == -1 ) {
		PyErr_Print();
		PySys_WriteStdout( "Failed inserting _scheduler into sys.modules" );
		exit( -1 );
	}
	Py_DecRef(sys_modules);

	if ( !m_schedulerModule )
	{
		PyErr_Print();
		PySys_WriteStdout( "Failed to import scheduler module\n" );
		exit( -1 );
	}

    // Import capsule
	m_api = SchedulerAPI();

	if( m_api == nullptr )
	{
		PySys_WriteStdout( "Failed to import scheduler capsule\n" );
		PyErr_Print();
		exit( -1 );
	}

    // Store api for use in tests
    s_schedulerApi = m_api;

    // Import scheduler ready for use in tests
    if (PyRun_SimpleString("import scheduler\n") != 0)
    {
		PyErr_Print();
		exit( -1 );
    }

    // Import extension module ready for use in tests
	if( PyRun_SimpleString( "import schedulertest\n" ) != 0 )
	{
		PyErr_Print();
		exit( -1 );
	}

    // Import for use in tests
    m_mainModule = PyImport_AddModule( "__main__" );
	if( m_mainModule == nullptr )
	{
		PyErr_Print();
		exit( -1 );
    }

    // Get a reference to the main scheduler
    m_mainScheduler = m_api->PyScheduler_GetScheduler();
}

void InterpreterWithSchedulerModule::TearDown()
{
	// Destroy main scheduler
	Py_DecRef( m_mainScheduler );

    // Destroy scheduler helper module
	Py_DecRef( m_schedulerModule );
    
	if( Py_FinalizeEx() < 0 )
	{
		PyErr_Print();
		exit( -1 );
	}
}

PyTaskletObject* InterpreterWithSchedulerModule::CreateTasklet()
{
	// Create callable
	EXPECT_EQ( PyRun_SimpleString( "def foo():\n"
								   "   print(\"bar\")\n" ),
			   0 );
	PyObject* fooCallable = PyObject_GetAttrString( m_mainModule, "foo" );
	EXPECT_NE( fooCallable, nullptr );
	EXPECT_TRUE( PyCallable_Check( fooCallable ) );

	// Create tasklet
	PyObject* taskletArgs = PyTuple_New( 1 );
	EXPECT_NE( taskletArgs, nullptr );
	EXPECT_EQ( PyTuple_SetItem( taskletArgs, 0, fooCallable ), 0 );
	PyTaskletObject* tasklet = m_api->PyTasklet_New( m_api->PyTaskletType, taskletArgs );
	EXPECT_NE( tasklet, nullptr );

	// Check type
	EXPECT_TRUE( m_api->PyTasklet_Check( reinterpret_cast<PyObject*>( tasklet ) ) );

	// Clean
	Py_XDECREF( taskletArgs );
	Py_XDECREF( fooCallable );

    return tasklet;
}
