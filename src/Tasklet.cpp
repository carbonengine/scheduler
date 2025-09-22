#include "Tasklet.h"

#include <CcpTelemetry.h>

#if _WIN32
// WinBase.h defines a Yield() macro which clashes with the method name on scheduler
#ifdef Yield
#undef Yield
#endif
#endif

#include "ScheduleManager.h"
#include "Channel.h"
#include "PyCallableWrapper.h"
#include "Utils.h"

Tasklet::Tasklet( PyObject* pythonObject, PyObject* taskletExitException, bool isMain ) :
	PythonCppType( pythonObject ),
	m_greenlet( nullptr ),
	m_callable( nullptr ),
	m_arguments( nullptr ),
	m_kwArguments( nullptr ),
	m_isMain( isMain ),
	m_transferInProgress( false ),
	m_scheduled( false ),
	m_alive( isMain ),
	m_blocktrap( false ),
	m_previous( nullptr ),
	m_next( nullptr ),
	m_threadId( -1 ),
	m_transferArguments( nullptr ),
	m_transferException( nullptr ),
	m_channelBlockedOn( nullptr ),
	m_blockedDirection( ChannelDirection::NEITHER ),
	m_blocked( false ),
	m_exceptionState( Py_None ),
	m_exceptionArguments( Py_None ),
	m_taskletExitException( taskletExitException ),
	m_paused( false ),
	m_taskletParent( nullptr ),
	m_firstRun( true ),
	m_timesSwitchedTo( 0 ),
	m_reschedule( RescheduleType::NONE ),
	m_taggedForRemoval( false ),
	m_previousBlocked( nullptr ),
	m_nextBlocked( nullptr ),
	m_scheduleManager( nullptr ),
	m_remove( false ),
	m_killPending( false ),
	m_restoreException( false ),
	m_lineNumber( 0 ),
	m_startTime( 0 ),
	m_endTime( 0 ),
	m_runTime( 0.0 ),
	m_highlighted( false ),
	m_dontRaise( false ),
	m_ContextManagerCallable( nullptr ),
	m_exceptionHandler(nullptr)
{
    // Update Tasklet counters
	s_totalAllTimeTaskletCount++;
	s_totalActiveTasklets++;

	PySys_WriteStdout( "CONSTRUCTING NEW TASKLET\n" );

    // If tasklet is not a scheduler tasklet then register the tasklet with the thread's ScheduleManager
	if( !m_isMain )
	{
		PySys_WriteStdout( "WE ARE NOT MAIN . . .\n" );
		SetScheduleManager( ScheduleManager::GetThreadScheduleManager() );
	}
}

Tasklet::~Tasklet()
{
	if( m_alive )
	{
		SetAlive( false );
	}

    // Decriment Tasklet Counter
	s_totalActiveTasklets--;

	Py_CLEAR( m_callable );

	Py_CLEAR( m_arguments );

    Py_CLEAR( m_kwArguments );

	Py_XDECREF( m_greenlet );

	Py_XDECREF( m_transferArguments );

    Py_XDECREF( m_ContextManagerCallable );

}

void Tasklet::SetNextBlocked(Tasklet* tasklet)
{
	m_nextBlocked = tasklet;
}

Tasklet* Tasklet::NextBlocked() const
{
	return m_nextBlocked;
}

void Tasklet::SetPreviousBlocked(Tasklet* tasklet)
{
	m_previousBlocked = tasklet;
}

Tasklet* Tasklet::PreviousBlocked() const
{
	return m_previousBlocked;
}

void Tasklet::SetKwArguments( PyObject* kwarguments )
{
	Py_CLEAR( m_kwArguments );
	m_kwArguments = kwarguments;
}

PyObject* Tasklet::KwArguments() const
{
	return m_kwArguments;
}

void Tasklet::SetToCurrentGreenlet()
{
	// Import Greenlet C-API
	PyGreenlet_Import();

	if( _PyGreenlet_API == NULL )
	{
		PySys_WriteStdout( "Failed to import greenlet capsule\n" );
		PyErr_Print();
	}

	Py_XDECREF( m_greenlet );

    m_greenlet = PyGreenlet_GetCurrent();
}

bool Tasklet::Remove()
{
	if(m_scheduled)
	{
		m_scheduleManager->RemoveTasklet( this );

		m_paused = true;

        return true;
    }
	else
	{
		return true;
    }
}

void Tasklet::OnCallableEntered()
{
	std::chrono::steady_clock::time_point current_time = std::chrono::steady_clock::now();
	m_startTime = current_time.time_since_epoch().count();
}

void Tasklet::OnCallableExited()
{
	std::chrono::steady_clock::time_point current_time = std::chrono::steady_clock::now();
	m_endTime = current_time.time_since_epoch().count();
}

bool Tasklet::Initialise()
{
	Py_XDECREF( m_greenlet );

	m_greenlet = PyGreenlet_New( m_callable, nullptr );

    SetCallable( nullptr );

    if (!m_greenlet)
    {
		return false;
    }
    else
    {
		m_paused = true;
		m_firstRun = true;

		return true; 
    }
}

void Tasklet::Uninitialise()
{
	Py_XDECREF( m_greenlet );
    
    m_greenlet = nullptr;
}

bool Tasklet::Insert()
{
	TelemetryZone telemetryZone(TMCM_CPP, "Tasklet::Insert()", __FILE__, __LINE__, tracy::Color::LightGreen);
    if ( m_blocked )
    {
		PyErr_SetString( PyExc_RuntimeError, "Failed to insert tasklet: Cannot insert blocked tasklet" );

		return false;
    }

    if ( !m_alive )
    {
		PyErr_SetString( PyExc_RuntimeError, "Failed to insert tasklet: Cannot insert dead tasklet" );

		return false;
    }

    m_scheduleManager->InsertTasklet( this );

	m_paused = false;

    return true;
 
}

bool Tasklet::SwitchImplementation()
{
	TelemetryZone telemetryZone(TMCM_CPP, "Tasklet::SwitchImplementation()", __FILE__, __LINE__, tracy::Color::LightGreen);
	// Remove the calling tasklet
	if( !m_alive )
	{
		PyErr_SetString( PyExc_RuntimeError, "You cannot run an unbound(dead) tasklet" );

		return false;
	}

	if( m_blocked )
	{
		PyErr_SetString( PyExc_RuntimeError, "Cannot switch to a tasklet that is blocked" );

		return false;
	}

    if( !BelongsToCurrentThread() )
	{
		PyErr_SetString( PyExc_RuntimeError, "Failed to switch tasklet: Cannot switch tasklet from another thread" );

		return false;
	}

	// Run scheduler starting from this tasklet (If it is already in the scheduled)
	if( m_scheduled )
	{
        // Pause the parent tasklet
		m_scheduleManager->GetCurrentTasklet()->m_paused = true;

		if( m_scheduleManager->Run( this ) )
		{
			// Yeild the tasklets parent as to not continue execution of the rest of this tasklet
            if ( !m_scheduleManager->Yield() )
            {
				return false;
            }

			return true;
        }
		else
		{
			m_scheduleManager->GetCurrentTasklet()->m_paused = false;

			return false;
        } 

	}
	else
	{
		m_scheduleManager->GetCurrentTasklet()->m_paused = true;

        m_scheduleManager->InsertTasklet( this );

        if (!m_scheduleManager->Run(this))
        {
			m_scheduleManager->GetCurrentTasklet()->m_paused = false;

			return false;
        }

        m_scheduleManager->GetCurrentTasklet()->m_paused = false;

	}

	return true;


}

bool Tasklet::SwitchTo( )
{
	
    bool ret = true;

    bool argsSuppled = false;
	bool kwArgsSupplied = false;

	if( m_arguments )
	{
		if( !PyTuple_Check( m_arguments ) )
		{
			PyErr_SetString( PyExc_RuntimeError, "Arguments must be a tuple" );

			return false;
		}

        argsSuppled = true;

	}

    if( m_kwArguments )
	{
		if( !PyDict_Check( m_kwArguments ) )
		{
			PyErr_SetString( PyExc_RuntimeError, "kwargs must be a dict" );

			return false;
		}

		kwArgsSupplied = true;
	}

    auto main_tasklet = m_scheduleManager->GetMainTasklet();
	// Check required arguments have been supplied if this is the first time the tasklet
	// Has been switch to
	if( m_firstRun && (main_tasklet != this) && !( kwArgsSupplied || argsSuppled ) )
	{
		PyErr_SetString( PyExc_RuntimeError, "No arguments supplied to tasklet" );

		return false;
	}

    ScheduleManager* scheduleManager = ScheduleManager::GetThreadScheduleManager();

	if( scheduleManager != m_scheduleManager)
	{
        // Tasklet being switched to is on a different thread than the current scheduleManager
        scheduleManager->InsertTasklet( this );

        if ( !scheduleManager->Yield() )
        {
            return false;
        }
    }
	else
	{

		if( m_scheduleManager->IsSwitchTrapped() )
		{
			PyErr_SetString( PyExc_RuntimeError, "Cannot schedule when scheduler switch_trap level is non-zero" );

			return false;
        }


        // If tasklet has never been run exceptions are treated differently
        if(( m_firstRun ) && (m_exceptionState != Py_None))
		{
			// If tasklet exit has been raised then don't run tasklet and keep silent
			if( PyErr_GivenExceptionMatches( m_exceptionState, m_taskletExitException ) )
			{
				SetAlive( false );

				return true;
            }
			else
			{
				SetPythonExceptionStateFromTaskletExceptionState();

                // Inform scheduler to remove this tasklet
                m_remove = true;

                return false;
            }
		
        }

        m_timesSwitchedTo++;

        // Tasklet is on the same thread so can be switched to now
		scheduleManager->SetCurrentTasklet( this );

        m_paused = false;

        PyObject* args = nullptr;
		PyObject* kwargs = nullptr;

        if (m_firstRun)
        {
			args = Arguments();
			kwargs = KwArguments();
            OnCallableEntered();
        }

        m_firstRun = false;

		ret = PyGreenlet_Switch( m_greenlet, args, kwargs );

        // Clear arguments
		SetArguments( nullptr );
		SetKwArguments( nullptr );
        
        // Check exception state of current tasklet
        // It is important to understand that the current tasklet may not be the same value as this object
        // This object will be the value of the tasklet that the other tasklet last switched to, commonly
        // This will be the scheduler. So when the tasklet is resumed it will resume from that context
        // We want to check the exception of the current tasklet so we need to get this value and check that

		Tasklet* currentTasklet = scheduleManager->GetCurrentTasklet();

		if( currentTasklet->m_exceptionState != Py_None )
		{

            currentTasklet->SetPythonExceptionStateFromTaskletExceptionState();

            return false;
  
        }

        // Check state of tasklet
        if( !m_blocked && !m_transferInProgress && !m_isMain && !m_paused && m_reschedule == RescheduleType::NONE && !m_taggedForRemoval ) 
		{
			SetAlive( false );

			OnCallableExited();
		}

		// Removed tasklet is paused
        if (m_taggedForRemoval)
        {
			m_paused = true;
        }

		// Reset tagging used to preserve alive status after removal
        m_taggedForRemoval = false;

        
		if( !ret )
		{
			// Inform scheduler to remove this tasklet
			m_remove = true;
		}

    }

	return ret;
}

void Tasklet::ClearException()
{
	if( m_exceptionState != Py_None)
	{
		Py_DecRef( m_exceptionState );

        m_exceptionState = Py_None;
    }

    if( m_exceptionArguments != Py_None )
	{
		Py_DecRef( m_exceptionArguments );

		m_exceptionArguments = Py_None;
    }
}

void Tasklet::SetExceptionState( PyObject* exception, PyObject* arguments /* = Py_None */ )
{
	ClearException();

    Py_IncRef( exception );
    m_exceptionState = exception;

    Py_IncRef( arguments );
	m_exceptionArguments = arguments;
}

// Assumes valid exception state
// Exception state validity is sanitised in PyTasklet_python.cpp
void Tasklet::SetPythonExceptionStateFromTaskletExceptionState()
{
	//If it is an instance of an exception
	if( PyObject_IsInstance( m_exceptionState, PyExc_Exception ) )
	{
        // PyErr_SetRaisedException steals reference to exception state
        // increffed to compensate so clear_exception will still work
		Py_IncRef( m_exceptionState ); 
		PyErr_SetRaisedException( m_exceptionState );
	}
	else
	{
		PyErr_SetObject( m_exceptionState, m_exceptionArguments );
	}	

	ClearException();
}

bool Tasklet::Run()
{
	if(!m_alive)
	{
		PyErr_SetString( PyExc_RuntimeError, "Cannot run tasklet that is not alive (dead)" );

		return false;
    }

    if( m_blocked )
	{
		PyErr_SetString( PyExc_RuntimeError, "Cannot run tasklet that is blocked" );

		return false;
	}

    if( !BelongsToCurrentThread() )
	{
        // Silently ignored from other thread
		return true;
	}

    if (ScheduleManager::s_useNestedTasklets)
    {
		// Run scheduler starting from this tasklet (If it is already in the scheduled)
		if( !m_scheduled )
		{
			m_scheduleManager->InsertTasklet( this );
		}
		return m_scheduleManager->Run( this );
    }
    else
    {
		bool requiresDeferredDecref = false;

        if (m_scheduled)
        {
			// Tasklet reference will be relinquished from the schedule manager queue
			m_scheduleManager->RemoveTasklet( this );

			requiresDeferredDecref = true;
        }

        // Insert the Tasklet back into the queue, but to run next. This will incref the Tasklet
		m_scheduleManager->InsertTaskletToRunNext( this );

        if( requiresDeferredDecref )
		{
			// Release relinquished reference from the RemoveTasklet above
			Decref();
		}

        // Reschedule Tasklet to run right after the Tasklet that is currently at the front
		return m_scheduleManager->Schedule( RescheduleType::FRONT_PLUS_ONE );
    }
	
}

bool Tasklet::Kill( bool pending /*=false*/ )
{
	if( !BelongsToCurrentThread() )
	{
		PyErr_SetString( PyExc_RuntimeError, "Failed to kill tasklet: Cannot kill tasklet from another thread" );

		return false;
	}

    // Quick out if tasklet isn't alive
    // the only reason we're NOT raising
    // a python exception here to stay
    // the same as stackless in this situation
    if( !m_alive )
	{
		return true;
	}

    // Quick out if kill is already pending
    if (m_killPending)
    {
		return true;
    }

    //Store so condition can be reinstated on failure
    bool blockedStore = m_blocked;
	Channel* blockChannelStore = m_channelBlockedOn;

    if(m_blocked)
	{
        Unblock();
    }
    
    // Raise TaskletExit error
	SetExceptionState( m_taskletExitException );

    if( m_scheduleManager->GetCurrentTasklet() == this )
	{
        // Continue on this tasklet and raise error immediately
		SetPythonExceptionStateFromTaskletExceptionState();

		return false;
    }
	else
	{
		if( pending )
		{
            m_scheduleManager->InsertTasklet( this );
			SetReschedule( RescheduleType::NONE );

            m_killPending = true;

            if( blockedStore )
			{
				blockChannelStore->UnblockTaskletFromChannel( this );

				SetBlockedDirection( ChannelDirection::NEITHER );
			}

            return true;
		}
		else
		{
			SetReschedule( RescheduleType::NONE );
			bool result = Run();

			if( result )
			{
				return true;
			}
			else
			{
				// Set tasklet back to original blocked state
				if( blockedStore )
				{

					Block( blockChannelStore );
				}

				return false;
			}
			
		}
	}

    return false;
}

PyObject* Tasklet::GetTransferArguments()
{
    //Ownership is relinquished
	PyObject* ret = m_transferArguments;

	return ret;
}

void Tasklet::ClearTransferArguments()
{

	m_transferArguments = nullptr;

}

void Tasklet::SetTransferArguments( PyObject* args, PyObject* exception, bool restoreException )
{
    //This should all change with the channel preference change
	if(m_transferArguments != nullptr)
	{
        //TODO this needs to be converted to an assert
		PySys_WriteStdout( "TRANSFER ARGS BROKEN %lu\n", PyThread_get_thread_ident() );
    }

	Py_IncRef( args );

	m_transferArguments = args;

    m_transferException = exception;
	m_restoreException = restoreException;
}

bool Tasklet::IsBlocked() const
{
	return m_blocked;
}

bool Tasklet::IsOnChannelBlockList() const
{
	return m_channelBlockedOn != nullptr || m_nextBlocked != nullptr || m_previousBlocked != nullptr;
}

void Tasklet::Block( Channel* channel )
{
	m_blocked = true;

    m_channelBlockedOn = channel;
}

void Tasklet::Unblock()
{
	m_blocked = false;

	m_channelBlockedOn = nullptr;
}

void Tasklet::SetAlive( bool value )
{
	if( value )
	{
		m_scheduleManager->RegisterTaskletToThread( this );
	}
	else
	{
		m_scheduleManager->UnregisterTaskletFromThread( this );
	}

	m_alive = value;
}

bool Tasklet::IsAlive() const
{
	return m_alive;
}

bool Tasklet::IsScheduled() const
{
	return m_scheduled;
}

void Tasklet::SetScheduled( bool value )
{
	m_scheduled = value;
}

bool Tasklet::IsBlocktrapped() const
{
	return m_blocktrap;
}

void Tasklet::SetBlocktrap( bool value )
{
	m_blocktrap = value;
}

bool Tasklet::IsMain() const
{
	return m_isMain;
}

void Tasklet::MarkAsMain( bool value )
{
	m_isMain = value;
}

unsigned long Tasklet::ThreadId() const
{
	return m_threadId;
}

Tasklet* Tasklet::Next() const
{
	return m_next;
}

void Tasklet::SetNext( Tasklet* next )
{
	m_next = next;
}

Tasklet* Tasklet::Previous() const
{
	return m_previous;
}

void Tasklet::SetPrevious( Tasklet* previous )
{
	m_previous = previous;
}

PyObject* Tasklet::Arguments() const
{
	return m_arguments;
}

void Tasklet::SetArguments( PyObject* arguments )
{
	Py_XDECREF( m_arguments );

	m_arguments = arguments;
}

bool Tasklet::TransferInProgress() const
{
	return m_transferInProgress;
}

void Tasklet::SetTransferInProgress( bool value )
{
	m_transferInProgress = value;
}

PyObject* Tasklet::TransferException() const
{
	return m_transferException;
}

bool Tasklet::ThrowException( PyObject* exception, PyObject* value, PyObject* tb, bool pending )
{

    if( !BelongsToCurrentThread() )
	{
		PyErr_SetString( PyExc_RuntimeError, "Failed to throw tasklet: Cannot throw tasklet from another thread" );

		return false;
	}

    SetExceptionState( exception, value );

    if( m_scheduleManager->GetCurrentTasklet() == this )
	{
		// Continue on this tasklet and raise error immediately
		SetPythonExceptionStateFromTaskletExceptionState();

        return false;
	}
	else
	{
		if( pending )
		{
			if(m_alive)
			{
                m_scheduleManager->InsertTasklet( this );
            }
			else
			{
				// If exception state is tasklet exit then handle silently
				if( PyErr_GivenExceptionMatches( m_exceptionState, m_taskletExitException ) )
				{
					ClearException();

					return true;
				}
				else
				{
					ClearException();

					PyErr_SetString( PyExc_RuntimeError, "You cannot throw to a dead tasklet." );

					return false;
				}
            }
			
		}
		else
		{
			if(m_blocked)
			{
				Channel* block_channel_store = m_channelBlockedOn;
				ChannelDirection blocked_direction_store = m_blockedDirection;

				Unblock();

				if(Run())
				{
					return true;
                }
				else
				{
                    // On failure return to original state
					Block( block_channel_store );

					return false;
                }
			}
			else
			{
                // Must be alive
                if(!m_alive)
				{
					// If exception state is tasklet exit then handle silently
					if( PyErr_GivenExceptionMatches( m_exceptionState, m_taskletExitException ) )
					{
						ClearException();

						return true;
					}
					else
					{
						ClearException();

						PyErr_SetString( PyExc_RuntimeError, "You cannot throw to a dead tasklet" );

						return false;
					}

                }
				else
				{
					bool ret = Run();

					return ret;
                }
            }
			
		}

		return true;
    }

}

bool Tasklet::IsPaused()
{
	return m_paused;
}

Tasklet* Tasklet::GetParent()
{
	return m_taskletParent;
}

bool Tasklet::SetParent( Tasklet* parent )
{
	if( parent )
	{
		parent->Incref();

	    int ret = PyGreenlet_SetParent( m_greenlet, parent->m_greenlet );

	    if( ret == -1 )
	    {
		    return false;
	    }

    }
    else
    {
        if (!m_isMain && m_greenlet)
        {
			Tasklet* main = m_scheduleManager->GetMainTasklet();

            main->Incref();

			int ret = PyGreenlet_SetParent( m_greenlet, main->m_greenlet );

			if( ret == -1 )
			{
				return false;
			}

        }
    }

    if (m_taskletParent)
    {
		m_taskletParent->Decref();
    }

	m_taskletParent = parent;

    return true;
}

bool Tasklet::TaskletExceptionRaised()
{
	PyObject* exception = PyErr_Occurred();
	if( exception )
    {
		return PyErr_ExceptionMatches( m_taskletExitException );
    }

    return false;
}

void Tasklet::ClearTaskletException()
{
    if (TaskletExceptionRaised())
    {
		ClearException();

		PyErr_Clear();
    }
}

void Tasklet::SetReschedule( RescheduleType value )
{
	m_reschedule = value;
}

RescheduleType Tasklet::RequiresReschedule()
{
	return m_reschedule;
}

void Tasklet::SetTaggedForRemoval( bool value )
{
	m_taggedForRemoval = value;
}

void Tasklet::SetCallable(PyObject* callable)
{
	Py_CLEAR( m_callable );

	m_callable = callable;
}

PyObject* Tasklet::GetCallable()
{
	return m_callable;
}

bool Tasklet::RequiresRemoval()
{
	return m_remove;
}

ChannelDirection Tasklet::GetBlockedDirection()
{
	return m_blockedDirection;
}

void Tasklet::SetBlockedDirection( ChannelDirection direction )
{
	m_blockedDirection = direction;
}

void Tasklet::SetScheduleManager( ScheduleManager* scheduleManager )
{
	if( !scheduleManager )
	{
		// Clean Up Tasklet if alive
		if( m_alive && !IsMain() )
		{
			Kill();
		}

		// If the Tasklet is now not alive then finish setting the ScheduleManager to
		// null, if not the Tasklet is still alive and still needs it's current ScheduleManager
		// It is not valid to have a Tasklet that is alive with no ScheduleManager
		if( !m_alive )
		{
			m_threadId = -1;
			m_scheduleManager = scheduleManager;
		}

		return;
	}
	else
	{
		m_threadId = scheduleManager->ThreadId();

		m_scheduleManager = scheduleManager;

		return;
	}

	return;
}

ScheduleManager* Tasklet::GetScheduleManager()
{
	return m_scheduleManager;
}

bool Tasklet::ShouldRestoreTransferException() const
{
	return m_restoreException;
}

bool Tasklet::Setup( PyObject* args, PyObject* kwargs )
{

    if( !BelongsToCurrentThread() )
	{
		PyErr_SetString( PyExc_RuntimeError, "Failed to setup tasklet: Cannot setup tasklet from another thread" );

		return false;
	}

    if (m_callable == nullptr)
    {
		PyErr_SetString( PyExc_RuntimeError, "This tasklet must be bound to a callable before you can call setup. Please call bind first." );

        return false;
    }

	Py_XINCREF( args );

    SetArguments( args );

    Py_XINCREF( kwargs );

    SetKwArguments( kwargs );

    if (!Initialise())
    {
		SetArguments( nullptr );

        SetKwArguments( nullptr );

        return false;
    }

    SetAlive( true );

    if (!Insert())
    {
		SetAlive( false );

        Uninitialise();

        SetArguments( nullptr );

        SetKwArguments( nullptr );

        return false;
    }

    return true;

}

bool Tasklet::SetCallsiteData( PyObject* callable )
{
	m_methodName = { "unknown_method" };
	m_moduleName = { "unknown_module" };
	m_fileName =   { "unknown_file" };
	m_lineNumber = { 0 };

	if( PyObject_HasAttrString( callable, "__name__" ) )
	{
		PyObject* dunderName = PyObject_GetAttrString( callable, "__name__" );

        // In most places, __name__ is a string.
        // But in some places in the python code, we are setting it to something else
		PyObject* nameString = PyObject_Str( dunderName );

        Py_DECREF( dunderName );

		if( !StdStringFromPyObject( nameString, m_methodName ) )
		{
			Py_DECREF( nameString );
			return false;
		}

        Py_DECREF( nameString );
	}

	
	if( PyObject_HasAttrString( callable, "__module__" ) )
	{
		PyObject* dunderModule = PyObject_GetAttrString( callable, "__module__" );
		PyObject* moduleString = PyObject_Str( dunderModule );

		Py_DECREF( dunderModule );

        // In most places, __module__ is a string.
        // But in some places in the python code, we are setting it to something else
		if( !StdStringFromPyObject( moduleString, m_moduleName ) )
		{
			Py_DECREF( moduleString );
			return false;
		}
        
		Py_DECREF( moduleString );
	}
   
    if (!PyObject_HasAttrString(callable, "__code__"))
    {
        return true;
    }

    PyObject* dunderCode = PyObject_GetAttrString( callable, "__code__" );

    if( PyObject_HasAttrString( dunderCode, "co_filename" ) )
	{
		PyObject* coFileName = PyObject_GetAttrString( dunderCode, "co_filename" );
		bool res = StdStringFromPyObject(coFileName , m_fileName );
		Py_DECREF( coFileName );
        
        if (!res)
        {
			Py_DECREF( dunderCode );
			return false;
        }
	}

	if( PyObject_HasAttrString( dunderCode, "co_firstlineno" ) )
	{
		PyObject* coLineNumber = PyObject_GetAttrString( dunderCode, "co_firstlineno" );
		m_lineNumber = PyLong_AsLong( coLineNumber );
		Py_DECREF( coLineNumber );

        if (PyErr_Occurred())
        {
			Py_DECREF( dunderCode );
			return false;
        }
	}

    Py_DECREF( dunderCode );

    return true;
}

bool Tasklet::Bind(PyObject* callable, PyObject* args, PyObject* kwargs)
{
	if( !BelongsToCurrentThread() )
	{
		PyErr_SetString( PyExc_RuntimeError, "Failed to bind tasklet: Cannot bind tasklet from another thread" );

		return false;
	}

    // Check callable is valid
	if( callable && callable != Py_None )
	{
        if (!PyCallable_Check(callable))
        {
			PyErr_SetString( PyExc_TypeError, "parameter must be callable" );
			return false;
        }
        else
        {
            if( !SetCallsiteData( callable ) )
			{
				return false;
			}

            if (m_dontRaise)
            {
				PyObject* wrapperArgsTuple = PyTuple_New( 1 );

				Py_IncRef( callable ); // PyTuple_SetItem steals a reference
				PyTuple_SetItem( wrapperArgsTuple, 0, callable );

				PyObject* pyCallableWrapper = PyObject_CallObject( reinterpret_cast<PyObject*>( ScheduleManager::s_callableWrapperType ), wrapperArgsTuple );

				Py_DECREF( wrapperArgsTuple );

				if( !pyCallableWrapper )
				{
					return false;
				}

                PyCallableWrapperObject* wrapper = reinterpret_cast<PyCallableWrapperObject*>( pyCallableWrapper );
				wrapper->m_ownerTasklet = this;

				SetCallable( pyCallableWrapper );
            }
            else
            {
				Py_IncRef( callable );
				SetCallable( callable );
            }
        }
    }

    // Process arguments
    bool argsSupplied = false;

    if( args )
    {
		if( args == Py_None )
		{
			args = PyTuple_New( 0 );

            if (!args)
            {
				PyErr_SetString( PyExc_TypeError, "internal error: Could not build empty tuple in place of PyNone for bind args" );

                SetCallable( nullptr );

				return false;
            }
		}
        else
        {
			Py_IncRef( args );
        }

        SetArguments( args );

		argsSupplied = true;
    }

    // Process kwargs
	bool kwargsSupplied = false;

    if (kwargs)
    {
		Py_IncRef( kwargs );

        SetKwArguments( kwargs );

        kwargsSupplied = true;
    }

    if (argsSupplied || kwargsSupplied)
    {
        if (!Initialise())
        {
			PyErr_SetString( PyExc_TypeError, "internal error: Failed to initialise Tasklet" );

            SetCallable( nullptr );

            SetArguments( nullptr );

            SetKwArguments( nullptr );

			return false;
        }

        SetAlive( true );
    }

    m_timesSwitchedTo = 0;

    return true;

}

bool Tasklet::UnBind()
{
	if( !BelongsToCurrentThread() )
	{
		PyErr_SetString( PyExc_RuntimeError, "Failed to unbind tasklet: Cannot unbind tasklet from another thread" );

		return false;
	}

	Tasklet* current = m_scheduleManager->GetCurrentTasklet();

	if( this == current )
	{
		PyErr_SetString( PyExc_RuntimeError, "cannot unbind current tasklet" );

		return false;
	}

    if (IsScheduled())
    {
		PyErr_SetString( PyExc_RuntimeError, "cannot unbind scheduled tasklet" );

        return false;
    }

    // Clear Callable
    SetCallable( nullptr );

    // Clear Arguments
	SetArguments( nullptr );

    // Clear Greenlet
	Uninitialise();

    // Mark as dead
	SetAlive( false );

    return true;
}

void Tasklet::Clear()
{
	// Clear Callable
	SetCallable( nullptr );

    // Clear Arguments
	SetArguments( nullptr );

    // Clear Arguments
	SetKwArguments( nullptr );
}

long Tasklet::GetAllTimeTaskletCount()
{
	return s_totalAllTimeTaskletCount;
}

long Tasklet::GetActiveTaskletCount()
{
	return s_totalActiveTasklets;
}

bool Tasklet::BelongsToCurrentThread()
{
	bool ret;

    ScheduleManager* scheduleManager = ScheduleManager::GetThreadScheduleManager();

    ret = scheduleManager == m_scheduleManager;

    return ret;
}

std::string Tasklet::GetMethodName()
{
	return m_methodName;
}

void Tasklet::SetMethodName(std::string& methodName)
{
	m_methodName = methodName;
}

std::string Tasklet::GetModuleName()
{
	return m_moduleName;
}

void Tasklet::SetModuleName(std::string& moduleName)
{
	m_moduleName = moduleName;
}

std::string Tasklet::GetContext()
{
    return m_context;
}

std::string Tasklet::GetFilename()
{
	return m_fileName;
}

void Tasklet::SetFilename( std::string& fileName )
{
	m_fileName = fileName;
}

long Tasklet::GetLineNumber()
{
	return m_lineNumber;
}

void Tasklet::SetLineNumber( long lineNumber )
{
	m_lineNumber = lineNumber;
}

void Tasklet::SetContext(std::string& context)
{
	m_context = context;
}


std::string Tasklet::GetParentCallsite()
{
	return m_parentCallsite;
}

void Tasklet::SetParentCallsite(std::string& parentCallsite)
{
	m_parentCallsite = parentCallsite;
}

long long Tasklet::GetStartTime()
{
	return m_startTime;
}

void Tasklet::SetStartTime( long long startTime )
{
	m_startTime = startTime;
}

long long Tasklet::GetEndTime()
{
	return m_endTime;
}

void Tasklet::SetEndTime( long long endTime )
{
	m_endTime = endTime;
}

double Tasklet::GetRunTime()
{
	return m_runTime;
}

void Tasklet::SetRunTime( double runTime )
{
	m_runTime = runTime;
}

bool Tasklet::GetHighlighted()
{
	return m_highlighted;
}

void Tasklet::SetHighlighted( bool highlighted )
{
	m_highlighted = highlighted;
}

bool Tasklet::GetDontRaise() const
{
	return m_dontRaise;
}

bool Tasklet::SetDontRaise( bool dontRaise )
{
	if( m_greenlet )
	{
		PyErr_SetString( PyExc_RuntimeError, "dont_raise cannot be altered after the Tasklet has been bound" );
		return false;
	}

	m_dontRaise = dontRaise;

	return true;
}

PyObject* Tasklet::GetContextManagerCallable() const
{
	return m_ContextManagerCallable;
}

void Tasklet::SetContextManagerCallable( PyObject* contextManagerCallable )
{
	Py_XDECREF( m_ContextManagerCallable );

	m_ContextManagerCallable = contextManagerCallable;
}


long Tasklet::GetTimesSwitchedTo()
{
	return m_timesSwitchedTo;
}

PyObject* Tasklet::GetExceptionHandler() const
{
	return m_exceptionHandler;
}

void Tasklet::SetExceptionHandler(PyObject* exceptionHander)
{
	Py_XDECREF( m_exceptionHandler );

    m_exceptionHandler = exceptionHander;
}
