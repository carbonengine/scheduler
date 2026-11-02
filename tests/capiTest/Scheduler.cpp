
#include "StdAfx.h"
#include <Python.h>
#include <Scheduler.h>

#include "InterpreterWithSchedulerModule.h"

struct SchedulerCapi : public InterpreterWithSchedulerModule{};

TEST_F( SchedulerCapi, PyScheduler_Schedule )
{
    // Create a test value
	EXPECT_EQ( PyRun_SimpleString( "testValue = [0]\n" ), 0 );

    // Create callable which schedules using c++
	EXPECT_EQ( PyRun_SimpleString( "def foo(remove):\n"
								   "   schedulertest.schedule(remove)\n"
								   "   testValue[0] = 1\n" ),
			   0 );

    // Create another callable for use in another tasklet
	EXPECT_EQ( PyRun_SimpleString( "def bar():\n"
								   "   testValue[0] = 2\n" ),
			   0 );

    // Create tasklets and run Specify no remove
	EXPECT_EQ( PyRun_SimpleString( "scheduler.tasklet(foo)(0)\n"
								   "scheduler.tasklet(bar)()\n"
								   "scheduler.run()\n" ),
			   0 );

    // Check tasklets are finished
	EXPECT_EQ( m_api->PyScheduler_GetRunCount(), 1 );

    // Check test value shows correct value
    PyObject* pythonTestValueList = PyObject_GetAttrString( m_mainModule, "testValue" );
	EXPECT_NE( pythonTestValueList, nullptr );
    EXPECT_TRUE( PyList_Check( pythonTestValueList ) );
    PyObject* pythonTestValue = PyList_GetItem( pythonTestValueList, 0 );
	EXPECT_NE( pythonTestValue, nullptr );
    EXPECT_TRUE( PyLong_Check( pythonTestValue ) );
    EXPECT_EQ( PyLong_AsLong( pythonTestValue ), 1 );
    Py_XDECREF( pythonTestValueList );

    // Check remove functionality
	EXPECT_EQ( PyRun_SimpleString( "tasklet = scheduler.tasklet(foo)(1)\n"
								   "scheduler.run()\n" ),
			   0 );

    PyObject* tasklet = PyObject_GetAttrString( m_mainModule, "tasklet" );
	EXPECT_NE( tasklet, nullptr );
    EXPECT_TRUE( m_api->PyTasklet_Check( tasklet ) );
    
    // Tasklet should still be alive
    EXPECT_EQ( m_api->PyTasklet_Alive( reinterpret_cast<PyTaskletObject*>( tasklet ) ), 1) ;

    // Clean
	Py_XDECREF( tasklet );
}

TEST_F( SchedulerCapi, PyScheduler_GetRunCount )
{
	int runCount = SchedulerAPI()->PyScheduler_GetRunCount();

	// Expected 1 is main
	EXPECT_EQ( runCount, 1 );

	// Create tasklet
	EXPECT_EQ( PyRun_SimpleString( "s = scheduler.tasklet(lambda: None)()\n" ), 0 );

	runCount = SchedulerAPI()->PyScheduler_GetRunCount();

	// Expected 2 (1 + new tasklet)
	EXPECT_EQ( runCount, 2 );

	// Run scheduler
	EXPECT_EQ( PyRun_SimpleString( "scheduler.run()\n" ), 0 );

	runCount = SchedulerAPI()->PyScheduler_GetRunCount();

	// 1 Expected all tasklets successfully run leaving main
	EXPECT_EQ( runCount, 1 );
}

TEST_F( SchedulerCapi, PyScheduler_GetCurrent )
{
	// Get main
	PyObject* currentTasklet = SchedulerAPI()->PyScheduler_GetCurrent();

    EXPECT_NE( currentTasklet, nullptr );

	EXPECT_TRUE( SchedulerAPI()->PyTasklet_IsMain( reinterpret_cast<PyTaskletObject*>( currentTasklet ) ) );
}

TEST_F( SchedulerCapi, PyScheduler_RunNTasklets )
{
    // Schedule 3 Tasklets and pump 1 at a time

    // Create a test value container
	EXPECT_EQ( PyRun_SimpleString( "testValue = [0]\n" ), 0 );

	// Create callable
	EXPECT_EQ( PyRun_SimpleString( "def foo(x):\n"
								   "   testValue[0] = testValue[0] + x\n" ),
			   0 );
	
    // Create three tasklets
	EXPECT_EQ( PyRun_SimpleString( "t1 = scheduler.tasklet(foo)(1)\n"
								   "t2 = scheduler.tasklet(foo)(2)\n"
								   "t3 = scheduler.tasklet(foo)(3)\n" ),
			   0 );

    // Check queue and run
	EXPECT_EQ( m_api->PyScheduler_GetRunCount(), 4 );

    // Run for time
	EXPECT_EQ( m_api->PyScheduler_RunNTasklets( 1 ), Py_None ); 

    EXPECT_EQ( m_api->PyScheduler_GetRunCount(), 3 );

    EXPECT_EQ( m_api->PyScheduler_RunNTasklets( 1 ), Py_None ); 

	EXPECT_EQ( m_api->PyScheduler_GetRunCount(), 2 );

    EXPECT_EQ( m_api->PyScheduler_RunNTasklets( 1 ), Py_None );

    // Check test value
	PyObject* pythonTestValueList = PyObject_GetAttrString( m_mainModule, "testValue" );
	EXPECT_NE( pythonTestValueList, nullptr );
	EXPECT_TRUE( PyList_Check( pythonTestValueList ) );
	PyObject* pythonTestValue = PyList_GetItem( pythonTestValueList, 0 );
	EXPECT_NE( pythonTestValue, nullptr );
	EXPECT_TRUE( PyLong_Check( pythonTestValue ) );
	EXPECT_EQ( PyLong_AsLong( pythonTestValue ), 6 );
	Py_XDECREF( pythonTestValueList );

}

TEST_F( SchedulerCapi, PyScheduler_RunForTime )
{
	// Schedule 3 Tasklets and pump until complete

	// Create a test value container
	EXPECT_EQ( PyRun_SimpleString( "testValue = [0]\n" ), 0 );

	// Create callable
	EXPECT_EQ( PyRun_SimpleString( "def foo(x):\n"
								   "   testValue[0] = testValue[0] + x\n" ),
			   0 );

	// Create three tasklets
	EXPECT_EQ( PyRun_SimpleString( "t1 = scheduler.tasklet(foo)(1)\n"
								   "t2 = scheduler.tasklet(foo)(2)\n"
								   "t3 = scheduler.tasklet(foo)(3)\n" ),
			   0 );

	// Check queue and run
	EXPECT_EQ( m_api->PyScheduler_GetRunCount(), 4 );

	// Run scheduler until finished
    while (m_api->PyScheduler_GetRunCount() > 1)
    {
		EXPECT_EQ( m_api->PyScheduler_RunWithTimeout( 10 ), Py_None );
    }

	// Check test value
	PyObject* pythonTestValueList = PyObject_GetAttrString( m_mainModule, "testValue" );
	EXPECT_NE( pythonTestValueList, nullptr );
	EXPECT_TRUE( PyList_Check( pythonTestValueList ) );
	PyObject* pythonTestValue = PyList_GetItem( pythonTestValueList, 0 );
	EXPECT_NE( pythonTestValue, nullptr );
	EXPECT_TRUE( PyLong_Check( pythonTestValue ) );
	EXPECT_EQ( PyLong_AsLong( pythonTestValue ), 6 );
	Py_XDECREF( pythonTestValueList );
}

TEST_F( SchedulerCapi, PyScheduler_SetChannelCallback )
{
	// Create a test value container
	EXPECT_EQ( PyRun_SimpleString( "testValue = [None,None,None,None]\n" ), 0 );

    // Create a channel callback
	EXPECT_EQ( PyRun_SimpleString( "def channel_callback(channel, tasklet, sending, willblock):\n"
								   "   testValue[0] = channel\n"
								   "   testValue[1] = tasklet\n"
								   "   testValue[2] = sending\n"
								   "   testValue[3] = willblock\n" ),
			   0 );

    // Get scheduler callback callable
	PyObject* callbackCallable = PyObject_GetAttrString( m_mainModule, "channel_callback" );
	EXPECT_NE( callbackCallable, nullptr );
	EXPECT_TRUE( PyCallable_Check( callbackCallable ) );

    // Set a channel callback to c-api
	EXPECT_EQ( m_api->PyScheduler_SetChannelCallback( callbackCallable ), 0);

    // Create callable to send over channel
	EXPECT_EQ( PyRun_SimpleString( "def send_test():\n"
								   "   channel.send(5)\n" ),
			   0 );

    // Create and call a send action on a channel to invoke callback
	EXPECT_EQ( PyRun_SimpleString( "channel = scheduler.channel()\n"
								   "tasklet = scheduler.tasklet(send_test)()\n"
								   "scheduler.run()" ),
			   0 );

    // Retreive test values
	PyObject* pythonTestValueList = PyObject_GetAttrString( m_mainModule, "testValue" );
	EXPECT_NE( pythonTestValueList, nullptr );
	EXPECT_TRUE( PyList_Check( pythonTestValueList ) );
	PyObject* callbackChannel = PyList_GetItem( pythonTestValueList, 0 );
	EXPECT_NE( callbackChannel, nullptr );
	PyObject* callbackTasklet = PyList_GetItem( pythonTestValueList, 1 );
	EXPECT_NE( callbackTasklet, nullptr );
	PyObject* callbackSending = PyList_GetItem( pythonTestValueList, 2 );
	EXPECT_NE( callbackSending, nullptr );
	PyObject* callbackWillBlock = PyList_GetItem( pythonTestValueList, 3 );
	EXPECT_NE( callbackWillBlock, nullptr );

    // Check test values against expected
	EXPECT_TRUE( m_api->PyChannel_Check( callbackChannel ) );

    PyObject* originalChannel = PyObject_GetAttrString( m_mainModule, "channel" );

    EXPECT_NE( originalChannel, nullptr );

    EXPECT_EQ( callbackChannel, originalChannel );

    EXPECT_TRUE( m_api->PyTasklet_Check( callbackTasklet ) );

    PyObject* originalTasklet = PyObject_GetAttrString( m_mainModule, "tasklet" );

    EXPECT_NE( originalTasklet, nullptr );

    EXPECT_EQ( callbackTasklet, originalTasklet );

    EXPECT_TRUE( PyBool_Check( callbackSending ) );

    EXPECT_TRUE( PyObject_IsTrue( callbackSending ) );

    EXPECT_TRUE( PyBool_Check( callbackWillBlock ) );

	EXPECT_TRUE( PyObject_IsTrue( callbackWillBlock ) );

    // Cleanup
	Py_XDECREF( pythonTestValueList );
	Py_XDECREF( originalChannel );
	Py_XDECREF( originalTasklet );
    Py_XDECREF( callbackCallable );
    
}

TEST_F( SchedulerCapi, PyScheduler_GetChannelCallback )
{
    // Create and channel callback
	EXPECT_EQ( PyRun_SimpleString( "def channel_callback(channel, tasklet, sending, willblock):\n"
								   "   pass\n" ),
			   0 );

    // Set channel callback
	EXPECT_EQ( PyRun_SimpleString( "scheduler.set_channel_callback(channel_callback)\n" ), 0 );

    // Get channel callback via c-api
	PyObject* channelCallbackCapi = m_api->PyScheduler_GetChannelCallback();
	EXPECT_NE( channelCallbackCapi, nullptr );

    // Get callable from python
	PyObject* channelCallbackPython = PyObject_GetAttrString( m_mainModule, "channel_callback" );
	EXPECT_NE( channelCallbackPython, nullptr );

    // Check they match
    EXPECT_EQ( channelCallbackCapi, channelCallbackPython );

    // Clean
    Py_XDECREF( channelCallbackPython );

}

TEST_F( SchedulerCapi, PyScheduler_SetScheduleCallback )
{

    // Create a test value container
	EXPECT_EQ( PyRun_SimpleString( "testValue = [None,None]\n" ), 0 );

    // Create a scheduler callback
	EXPECT_EQ( PyRun_SimpleString( "def schedule_callback(prev,next):\n"
								   "   testValue[0] = prev\n"
								   "   testValue[1] = next\n" ),
			   0 );

    // Store main tasklet
	PyObject* mainTasklet = m_api->PyScheduler_GetCurrent();

	// Get scheduler callback callable
	PyObject* callbackCallable = PyObject_GetAttrString( m_mainModule, "schedule_callback" );
	EXPECT_NE( callbackCallable, nullptr );
	EXPECT_TRUE( PyCallable_Check( callbackCallable ) );

	// Set a scheduler callback to c-api
	EXPECT_EQ( m_api->PyScheduler_SetScheduleCallback( callbackCallable ), 0 );

	// Create and run tasklet
	EXPECT_EQ( PyRun_SimpleString( "tasklet = scheduler.tasklet(lambda: None)()\n"
								   "scheduler.run()\n" ),
			   0 );

	// Get reference to the tasklet
	PyObject* tasklet = PyObject_GetAttrString( m_mainModule, "tasklet" );
	EXPECT_NE( tasklet, nullptr );
	EXPECT_TRUE( m_api->PyTasklet_Check( tasklet ) );

	// Get test value
	PyObject* pythonTestValueList = PyObject_GetAttrString( m_mainModule, "testValue" );
	EXPECT_NE( pythonTestValueList, nullptr );
	EXPECT_TRUE( PyList_Check( pythonTestValueList ) );
	PyObject* pythonPreviousTasklet = PyList_GetItem( pythonTestValueList, 0 );
	EXPECT_NE( pythonPreviousTasklet, nullptr );
	PyObject* pythonNextTasklet = PyList_GetItem( pythonTestValueList, 1 );
	EXPECT_NE( pythonNextTasklet, nullptr );

	// Test object should be the same as the tasklet
	EXPECT_TRUE( m_api->PyTasklet_Check( pythonPreviousTasklet ) );
	EXPECT_TRUE( m_api->PyTasklet_Check( pythonNextTasklet ) );

	// Should match the tasklet
	EXPECT_EQ( pythonPreviousTasklet, tasklet );
	EXPECT_EQ( pythonNextTasklet, mainTasklet );

	// Cleanup
	Py_XDECREF( pythonTestValueList );
	Py_XDECREF( callbackCallable );
	Py_XDECREF( tasklet );
	Py_XDECREF( mainTasklet );

	// Reset the scheduler callback
	EXPECT_EQ( m_api->PyScheduler_SetScheduleCallback( nullptr ), 0 );

}

static PyTaskletObject* s_testFrom = nullptr;

static PyTaskletObject* s_testTo = nullptr;

static int FastCallback( struct PyTaskletObject* from, struct PyTaskletObject* to )
{
	s_testFrom = from;

    s_testTo = to;

	return 0;
}

TEST_F( SchedulerCapi, PyScheduler_SetScheduleFastcallback )
{
	m_api->PyScheduler_SetScheduleFastCallback( FastCallback );

    // Create and run tasklet
	EXPECT_EQ( PyRun_SimpleString( "tasklet = scheduler.tasklet(lambda: None)()\n"
								   "scheduler.run()\n" ),
			   0 );

    // Get reference to the tasklet
	PyObject* tasklet = PyObject_GetAttrString( m_mainModule, "tasklet" );
	EXPECT_NE( tasklet, nullptr );
	EXPECT_TRUE( m_api->PyTasklet_Check( tasklet ) );

    // Get main tasklet
	PyObject* mainTasklet = m_api->PyScheduler_GetCurrent();

    // Check values
    EXPECT_EQ( s_testFrom, reinterpret_cast<PyTaskletObject*>(tasklet) );
	EXPECT_EQ( s_testTo, reinterpret_cast<PyTaskletObject*>( mainTasklet ) );

    // Clean
	Py_XDECREF( tasklet );
	Py_XDECREF( mainTasklet );
}

TEST_F( SchedulerCapi, PyScheduler_GetNumberOfActiveScheduleManagers )
{
	int numberOfScheduleManagers = m_api->PyScheduler_GetNumberOfActiveScheduleManagers( );

    EXPECT_EQ( numberOfScheduleManagers, 1 );
}

TEST_F( SchedulerCapi, PyScheduler_GetNumberOfActiveChannels )
{
	EXPECT_EQ( m_api->PyScheduler_GetNumberOfActiveChannels(), 0 );

	PyChannelObject* channel1 = m_api->PyChannel_New( m_api->PyChannelType );

    EXPECT_EQ( m_api->PyScheduler_GetNumberOfActiveChannels(), 1 );

	PyChannelObject* channel2 = m_api->PyChannel_New( m_api->PyChannelType );

    EXPECT_EQ( m_api->PyScheduler_GetNumberOfActiveChannels(), 2 );

    Py_DECREF( channel1 );

    EXPECT_EQ( m_api->PyScheduler_GetNumberOfActiveChannels(), 1 );

    Py_DECREF( channel2 );

    EXPECT_EQ( m_api->PyScheduler_GetNumberOfActiveChannels(), 0 );
}

TEST_F( SchedulerCapi, PyScheduler_GetAllTimeTaskletCount )
{
    // 1 expected initially is the main Tasklet
	EXPECT_EQ( m_api->PyScheduler_GetAllTimeTaskletCount(), 1 );

	PyTaskletObject* tasklet1 = CreateTasklet();

	EXPECT_EQ( m_api->PyScheduler_GetAllTimeTaskletCount(), 2 );

	PyTaskletObject* tasklet2 = CreateTasklet();

	EXPECT_EQ( m_api->PyScheduler_GetAllTimeTaskletCount(), 3 );

	Py_DECREF( tasklet1 );

	EXPECT_EQ( m_api->PyScheduler_GetAllTimeTaskletCount(), 3 );

	Py_DECREF( tasklet2 );

	EXPECT_EQ( m_api->PyScheduler_GetAllTimeTaskletCount(), 3 );
}

TEST_F( SchedulerCapi, PyScheduler_GetActiveTaskletCount )
{
	EXPECT_EQ( m_api->PyScheduler_GetActiveTaskletCount(), 1 );

	PyTaskletObject* tasklet1 = CreateTasklet();

	EXPECT_EQ( m_api->PyScheduler_GetActiveTaskletCount(), 2 );

	PyTaskletObject* tasklet2 = CreateTasklet();

	EXPECT_EQ( m_api->PyScheduler_GetActiveTaskletCount(), 3 );

	Py_DECREF( tasklet1 );

	EXPECT_EQ( m_api->PyScheduler_GetActiveTaskletCount(), 2 );

	Py_DECREF( tasklet2 );

	EXPECT_EQ( m_api->PyScheduler_GetActiveTaskletCount(), 1 );
}

TEST_F( SchedulerCapi, PyScheduler_GetTaskletsCompletedLastRunWithTimeout )
{
	// Schedule 3 Tasklets and pump until complete

	// Create three tasklets
	EXPECT_EQ( PyRun_SimpleString( "t1 = scheduler.tasklet(lambda:None)()\n"
								   "t2 = scheduler.tasklet(lambda:None)()\n"
								   "t3 = scheduler.tasklet(lambda:None)()\n" ),
			   0 );

	// Check queue and run
	EXPECT_EQ( m_api->PyScheduler_GetRunCount(), 4 );

	// Run scheduler until finished using a huge timeout time
	EXPECT_EQ( m_api->PyScheduler_RunWithTimeout( 1000000 ), Py_None );

    // Ensure all tasklets complete in time
    EXPECT_EQ( m_api->PyScheduler_GetRunCount(), 1 );

    // Check tasklets completed since last timeout
	EXPECT_EQ( m_api->PyScheduler_GetTaskletsCompletedLastRunWithTimeout(), 3 );

}

TEST_F( SchedulerCapi, PyScheduler_GetTaskletsSwitchedLastRunWithTimeout )
{
	// Schedule 3 Tasklets and pump until complete

	// Create three tasklets
	EXPECT_EQ( PyRun_SimpleString( "t1 = scheduler.tasklet(lambda:None)()\n"
								   "t2 = scheduler.tasklet(lambda:None)()\n"
								   "t3 = scheduler.tasklet(lambda:None)()\n" ),
			   0 );

	// Check queue and run
	EXPECT_EQ( m_api->PyScheduler_GetRunCount(), 4 );

	// Run scheduler until finished using a huge timeout time
	EXPECT_EQ( m_api->PyScheduler_RunWithTimeout( 1000000 ), Py_None );

	// Ensure all tasklets complete in time
	EXPECT_EQ( m_api->PyScheduler_GetRunCount(), 1 );

	// Check tasklets completed since last timeout
	// This shows a switchting to and from the main tasklet
	EXPECT_EQ( m_api->PyScheduler_GetTaskletsSwitchedLastRunWithTimeout(), 6 );
}