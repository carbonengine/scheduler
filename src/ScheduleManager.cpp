#include "ScheduleManager.h"

#include <CcpTelemetry.h>

#include "Tasklet.h"
#include "PyTasklet.h"
#include "PyScheduleManager.h"
#include "GILRAII.h"

ScheduleManager::ScheduleManager( PyObject* pythonObject ) :
	PythonCppType( pythonObject ),
	m_threadId( PyThread_get_thread_ident() ),
	m_schedulerTasklet( nullptr ), // Created in constructor
	m_currentTasklet( nullptr ),   // Set in constructor
	m_previousTasklet( nullptr ),  // Set in constructor
	m_switchTrapLevel(0),
    m_taskletLimit(-1),
	m_totalTaskletRunTimeLimit(-1),
    m_stopScheduler(false),
	m_numberOfTaskletsInQueue(0),
	m_firstTimeLimitTestSkipped(false),
	m_runType(RunType::STANDARD),
	m_startTime( std::chrono::steady_clock::now() )
{
    // Create scheduler tasklet
	CreateSchedulerTasklet();

    m_currentTasklet = m_schedulerTasklet;

	m_previousTasklet = m_schedulerTasklet;

}

ScheduleManager::~ScheduleManager()
{
	s_closingScheduleManagers[m_threadId] = this;
	
    //Clear any Tasklets that may be remaining and associated with this Thread
	ClearThreadTasklets();
    
	s_closingScheduleManagers.erase( m_threadId );

	m_schedulerTasklet->Decref();

    s_numberOfActiveScheduleManagers--;
}

void ScheduleManager::CreateSchedulerTasklet()
{
	PyObject* taskletArgs = PyTuple_New( 2 );

	Py_IncRef( Py_None );

	PyTuple_SetItem( taskletArgs, 0, Py_None );

	PyTuple_SetItem( taskletArgs, 1, Py_True );

	PyObject* pySchedulerTasklet = PyObject_CallObject( reinterpret_cast<PyObject*>( s_taskletType ), taskletArgs );

	Py_DecRef( taskletArgs );

	m_schedulerTasklet = reinterpret_cast<PyTaskletObject*>( pySchedulerTasklet )->m_implementation;


	m_schedulerTasklet->SetToCurrentGreenlet();

	m_schedulerTasklet->SetScheduled( true );
}

long ScheduleManager::NumberOfActiveScheduleManagers()
{
	return s_numberOfActiveScheduleManagers;
}

// Returns a new schedule manager reference
ScheduleManager* ScheduleManager::GetThreadScheduleManager()
{

    GILRAII gil; // we MUST hold the gil - this is being extra safe
    
    // When a thread is destroyed it will cause ScheduleManager destruction to be called
	// The destructor attempts to clean up Tasklets on the ScheduleManager and this requires
	// calls to GetThreadScheduleManager. At this point when GetThreadScheduleManager is called
	// the ScheduleManager will not be found in the thread dict and so a new one would be made.
	// This means that subsequent equality checks will indicate a thread missmatch
	// To stop this a temporary container of closing scheduleManagers is held using the threadId
	// as a key. We are not in danger of the threadId being reused at this point as technically
	// the thread will not have fully finished until the scheduleManager destructor is completed
	// at the end of which the scheduleManager will be removed from the closingScheduleManagers list.
	long threadId = PyThread_get_thread_ident();
	auto res = s_closingScheduleManagers.find( threadId );
	if( res != s_closingScheduleManagers.end() )
	{
		return res->second;
	}

    PyObject* threadDict = PyThreadState_GetDict();
    
    PyObject* pyScheduleManager = PyDict_GetItem( threadDict, m_scheduleManagerThreadKey );

    ScheduleManager* scheduleManager = nullptr;

    if( !pyScheduleManager )
	{
		// Create new scheduler for the thread
		pyScheduleManager = PyObject_CallObject( reinterpret_cast<PyObject*>( s_scheduleManagerType ), nullptr );

		scheduleManager = reinterpret_cast<PyScheduleManagerObject*>( pyScheduleManager )->m_implementation;

        scheduleManager->m_schedulerTasklet->SetScheduleManager( scheduleManager );

		int res = PyDict_SetItem( threadDict, m_scheduleManagerThreadKey, pyScheduleManager );
        
        scheduleManager->Decref();

        if (res == -1)
        {
			scheduleManager->Decref();
			return nullptr;
        }

        s_numberOfActiveScheduleManagers++;
	}
	else
	{
		scheduleManager = reinterpret_cast<PyScheduleManagerObject*>( pyScheduleManager )->m_implementation;
    }

    return scheduleManager;
}

void ScheduleManager::SetCurrentTasklet( Tasklet* tasklet )
{
    if (m_currentTasklet != tasklet)
    {
		OnSwitch();

		RunSchedulerCallback( m_currentTasklet, tasklet );

		m_currentTasklet = tasklet;
    }
}

Tasklet* ScheduleManager::GetCurrentTasklet()
{
	return m_currentTasklet;
}

void ScheduleManager::InsertTaskletToRunNext( Tasklet* tasklet )
{
	tasklet->Incref();

	if( tasklet->IsScheduled() )
	{
		tasklet->SetReschedule( RescheduleType::BACK );
		return;
	}

	Tasklet* currentTasklet = ScheduleManager::GetCurrentTasklet();

	tasklet->SetNext( currentTasklet->Next() );
	tasklet->SetPrevious( currentTasklet );

	currentTasklet->SetNext( tasklet );

	if( tasklet->Next() )
	{
		tasklet->Next()->SetPrevious( tasklet );
	}

	if( tasklet->Previous() == m_previousTasklet )
	{
		m_previousTasklet = tasklet;
	}
	tasklet->Unblock();
	tasklet->SetScheduled( true );

	m_numberOfTaskletsInQueue++;
}

void ScheduleManager::InsertTasklet( Tasklet* tasklet )
{
	ScheduleManager* taskletScheduleManager = tasklet->GetScheduleManager();

    if( !tasklet->IsScheduled() )
	{
		tasklet->Incref();

		taskletScheduleManager->m_previousTasklet->SetNext( tasklet );

		tasklet->SetPrevious( taskletScheduleManager->m_previousTasklet );

		// Clear out possible old next
		tasklet->SetNext( nullptr );

		taskletScheduleManager->m_previousTasklet = tasklet;

		tasklet->Unblock();	// TODO should probably not be here and replaced with error path

		tasklet->SetScheduled( true );

        m_numberOfTaskletsInQueue++;
    }
	else
	{
		tasklet->SetReschedule( RescheduleType::BACK );
	}
}

// Relinquishes reference ownership of Tasklet
bool ScheduleManager::RemoveTasklet( Tasklet* tasklet )
{
	Tasklet* previous = tasklet->Previous();

    Tasklet* next = tasklet->Next();

    if (previous == next)
    {
		return false;
    }

    if(previous != nullptr)
	{
		previous->SetNext( next );
	}
    
    if(next != nullptr)
	{
		next->SetPrevious( previous );
    }
    else
    {
		m_previousTasklet = previous;
    }

    m_numberOfTaskletsInQueue--;

	tasklet->SetNext( nullptr );

	tasklet->SetPrevious( nullptr );

    tasklet->SetScheduled( false );

    return true;
}

bool ScheduleManager::Schedule( RescheduleType position, bool remove /* = false */)
{
    // Add Current to the end of chain of runnable tasklets    
	Tasklet* currentTasklet = ScheduleManager::GetCurrentTasklet();


    if(remove)
	{
		// Set tag for removal flag, this flag will ensure tasklet remains alive after removal
		currentTasklet->SetTaggedForRemoval( true );
    }
	else
	{
		// Set reschedule flag to inform scheduler that this tasklet must be re-inserted
		currentTasklet->SetReschedule( position );
    }
    

    return Yield();

}

int ScheduleManager::GetCachedTaskletCount()
{
	return m_numberOfTaskletsInQueue + 1;   // +1 is the main tasklet
}

int ScheduleManager::GetCalculatedTaskletCount()
{
	int count = 0;

	Tasklet* currentTasklet = ScheduleManager::GetMainTasklet();

	while( currentTasklet->Next() != nullptr )
	{
		count++;
		currentTasklet = currentTasklet->Next();
	}

	return count + 1; // +1 is the main tasklet
}

// Returns true if tasklet is in a clean state when resumed
// Returns false if exception has been raised on tasklet
bool ScheduleManager::Yield()
{
	Tasklet* yieldingTasklet = ScheduleManager::GetCurrentTasklet();

	if( ScheduleManager::GetMainTasklet() == yieldingTasklet )
	{

		if( yieldingTasklet->IsBlocked() && yieldingTasklet->Next() == nullptr )
		{
			PyErr_SetString(PyExc_RuntimeError, "Deadlock: the last runnable tasklet cannot be blocked.");

			return false;
		}
		else if( yieldingTasklet->IsBlocked() )
        {
			bool success = ScheduleManager::Run();

            // if the run set an exception in python, we should fail due to that error now
            if( !success )
            {
				return false;
            }

            // if the main tasklet is still blocked, then this is a deadlock
			if( yieldingTasklet->IsBlocked() )
            {
				PyErr_SetString( PyExc_RuntimeError, "Deadlock: the last runnable tasklet cannot be blocked." );

				return false;
            }

            return success;
        }
        
		return ScheduleManager::Run();
	}
	else
	{
        //Switch to the parent tasklet - support for nested run and schedule calls
		Tasklet* parent_tasklet = yieldingTasklet->GetParent();

        parent_tasklet->Incref();

        if (!parent_tasklet->SwitchTo())
		{
			parent_tasklet->Decref();

			return false;
		}

        parent_tasklet->Decref();

        // guard against re-entry to this tasklet, if it is still blocked
        while (yieldingTasklet->IsBlocked())
		{
			auto parentTasklet = yieldingTasklet->GetParent();
            while (parentTasklet->IsBlocked() && !parentTasklet->IsMain())
            {
				parentTasklet = parentTasklet->GetParent();
            }

            parentTasklet->Incref();

			if( !parentTasklet->SwitchTo() )
			{
				return false;

                parentTasklet->Decref();
			}

            parentTasklet->Decref();
        }
	}

	return true;
}

bool ScheduleManager::RunTaskletsForTime( long long timeout )
{
	s_numberOfTaskletsCompletedLastRunWithTimeout = 0;

    s_numberOfTaskletsSwitchedLastRunWithTimeout = 0;

	m_totalTaskletRunTimeLimit = timeout;

    m_firstTimeLimitTestSkipped = false;

    m_runType = RunType::TIME_LIMITED;

    m_startTime = std::chrono::steady_clock::now();

	bool ret = Run();

    m_runType = RunType::STANDARD;

	m_stopScheduler = false;

	m_taskletLimit = -1;

    m_totalTaskletRunTimeLimit = -1;

	return ret;
}

bool ScheduleManager::RunNTasklets( int n )
{
	TelemetryZone telemetryZone(TMCM_CPP, "ScheduleManager::Run()", __FILE__, __LINE__, tracy::Color::LightGreen);
    m_taskletLimit = n;

    m_runType = RunType::TASKLET_LIMITED;

    bool ret = Run();

    m_runType = RunType::STANDARD;

    m_stopScheduler = false;

    m_taskletLimit = -1;

    return ret;
}

bool ScheduleManager::Run( Tasklet* startTasklet /* = nullptr */ )
{
	TelemetryZone telemetryZone(TMCM_CPP, "ScheduleManager::Run()", __FILE__, __LINE__, tracy::Color::LightGreen);
    Tasklet* baseTasklet = nullptr;

    Tasklet* endTasklet = nullptr;

	if( startTasklet )
	{
		baseTasklet = startTasklet->Previous();

        endTasklet = m_previousTasklet;
    }
	else
	{
		baseTasklet = GetCurrentTasklet();
    }

    bool runComplete = false;

    bool runUntilUnblocked = false;

    if (GetCurrentTasklet() == GetMainTasklet() && GetCurrentTasklet()->IsBlocked())
    {
		runUntilUnblocked = true;
    }

    while( ( baseTasklet->Next() != nullptr ) && ( !runComplete ) )
	{

        if( m_stopScheduler )
		{
			// Stop processing schedule queue
			Tasklet* activeTasklet = ScheduleManager::GetCurrentTasklet();

			if( activeTasklet == ScheduleManager::GetMainTasklet() )
			{
				break;
			}
		}

		Tasklet* currentTasklet = baseTasklet->Next();

        if (ScheduleManager::GetCurrentTasklet() == currentTasklet)
        {
            // Stop cyclic parent chain error
            // Early out with no error
			return true;
        }

        if (!currentTasklet->SetParent(ScheduleManager::GetCurrentTasklet()))
        {
			return false;
        }
		
        // If set to true then tasklet will be decreffed at the end of the loop
        bool cleanupCurrentTasklet = false;

        // Test Tasklet Run Limit
		// Currently only checking this for main tasklets as the teardown buildup
		// It is possible to extend behaviour to work with Nested Tasklets however
        // In order to do so the Main Tasklet needs the ability to recreate it's
        // State at switch. This is possible in a few ways but leads to code
        // obfuscation. This is not an issue when turning off Nested Tasklets
        // and with the hope that we will be moving away from Nested Tasklets
        // and the lack of urgency from the game, it is best to keep code simple. 
		if( GetCurrentTasklet()->IsMain() )
		{
            if (m_runType == RunType::TASKLET_LIMITED)
            {
				if( m_taskletLimit > 0 )
				{
					m_taskletLimit--;
				}
				if( m_taskletLimit <= 0 )
				{
					m_stopScheduler = true;
				}
            }
            else if (m_runType == RunType::TIME_LIMITED)
            {
				// Test Total tasklet Run Limit
				std::chrono::steady_clock::time_point current_time = std::chrono::steady_clock::now();

				if( std::chrono::duration_cast<std::chrono::nanoseconds>( current_time - m_startTime ).count() >= m_totalTaskletRunTimeLimit )
				{
					if( m_firstTimeLimitTestSkipped == false )
					{
						// The first time limit test is forced to succeed.
						// This is to ensure that at least one Tasklet is processed
						// Even if time limit is set to 0
						// This makes sense so we always progress and also matches
						// Stackless behaviour for run for time
						m_firstTimeLimitTestSkipped = true;
					}
					else
					{
						m_stopScheduler = true;
					}
				}
            }
		}

        // If switch returns no error or if the error raised is a tasklet exception raised error
		if( currentTasklet->SwitchTo() || currentTasklet->TaskletExceptionRaised() )
		{
			//Clear possible tasklet exception to capture
			currentTasklet->ClearTaskletException();

            if (runUntilUnblocked && !GetMainTasklet()->IsBlocked())
            {
				runComplete = true;
            }

			// Update current tasklet
			ScheduleManager::SetCurrentTasklet( currentTasklet->GetParent() );
            

			//If this is the last tasklet then update previous_tasklet to keep it at the end of the chain
			if( currentTasklet->Next() == nullptr )
			{
				m_previousTasklet = currentTasklet->Previous();
			}

			// Remove tasklet from queue
            if (RemoveTasklet(currentTasklet))
            {
				cleanupCurrentTasklet = true;
            }

            currentTasklet->SetScheduled( false );

			//Will this get skipped if it happens to be when it will schedule
			if( currentTasklet->RequiresReschedule() == RescheduleType::BACK )
			{
				InsertTasklet( currentTasklet );
				currentTasklet->SetReschedule( RescheduleType::NONE );
			}
            else if (currentTasklet->RequiresReschedule() == RescheduleType::FRONT_PLUS_ONE)
            {
				// Add after current next on queue
				Tasklet* front = GetCurrentTasklet()->Next();
				// Remove the current front as this will need to be retained
                // Reference will be relinquished to here
				RemoveTasklet( front );
				// current becomes second on queue
				InsertTaskletToRunNext( currentTasklet );
				// Reinstate the front again
				InsertTaskletToRunNext( front );
                // Decref the reference that was relinquished from RemoveTasklet above.
                front->Decref();
                // Reset reschedule flag
                currentTasklet->SetReschedule( RescheduleType::NONE );
            }
        }
		// Switch was unsuccessful
		else
		{
			// If exception state should lead to removal of tasklet
            if( currentTasklet->RequiresRemoval() )
			{
				// Update current tasklet
				ScheduleManager::SetCurrentTasklet( currentTasklet->GetParent() );

				if( RemoveTasklet( currentTasklet ) )
				{
					currentTasklet->SetParent( nullptr );   // TODO handle failure

					currentTasklet->Decref();
				}
			}
            else
            {
				currentTasklet->SetParent( nullptr ); // TODO handle failure
            }
            
			return false;
        }

        // Tasklets created during this run are not run in this loop
		if( currentTasklet == endTasklet )
		{
			runComplete = true;
		}

        // Same needs to happen in fail case
        if (!currentTasklet->IsAlive())
        {
			currentTasklet->SetParent( nullptr );   // TODO handle failure
        }

        if( cleanupCurrentTasklet )
		{
			currentTasklet->Decref();

            if (m_runType == RunType::TIME_LIMITED)
            {
                // Increament tasklet completed value
				s_numberOfTaskletsCompletedLastRunWithTimeout++;
            }
        }
		
	}

	return true;
}

void ScheduleManager::OnSwitch()
{
	if( m_runType == RunType::TIME_LIMITED )
	{
		// Increament tasklet switched value
		// Note this will also increment if a switch was blocked by switchtrap
		// It is more of an attempted switch value
		s_numberOfTaskletsSwitchedLastRunWithTimeout++;
	}
}

Tasklet* ScheduleManager::GetMainTasklet()
{
	return m_schedulerTasklet;
}

void ScheduleManager::SetSchedulerFastCallback( schedule_hook_func* func )
{
	s_schedulerFastCallback = func;
}

void ScheduleManager::SetSchedulerCallback( PyObject* callback )
{
	// Callback is common accross threads
	Py_XDECREF( s_schedulerCallback );

	s_schedulerCallback = callback;
}

void ScheduleManager::RunSchedulerCallback( Tasklet* previous, Tasklet* next )
{
    // Run Callback through python
	if(s_schedulerCallback)
	{
		PyObject* args = PyTuple_New( 2 );

        PyObject* pyPrevious = Py_None;

        if (previous)
        {
			pyPrevious = previous->PythonObject();
        }

        PyObject* pyNext = Py_None;

        if (next)
        {
			pyNext = next->PythonObject();
        }

        Py_IncRef( pyPrevious );

		Py_IncRef( pyNext );

        PyTuple_SetItem( args, 0, pyPrevious );

        PyTuple_SetItem( args, 1, pyNext );

		PyObject_Call( s_schedulerCallback, args, nullptr );

        Py_DecRef( args );
    }

    // Run fast callback bypassing python
    if (s_schedulerFastCallback)
    {
		s_schedulerFastCallback( reinterpret_cast<PyTaskletObject*>(previous->PythonObject()), reinterpret_cast<PyTaskletObject*>(next->PythonObject()) );
    }
}

bool ScheduleManager::IsSwitchTrapped()
{
    return m_switchTrapLevel != 0;
}

PyObject* ScheduleManager::SchedulerCallback()
{
	return s_schedulerCallback;
}

int ScheduleManager::SwitchTrapLevel()
{
	return m_switchTrapLevel;
}

void ScheduleManager::SetSwitchTrapLevel( int level )
{
	m_switchTrapLevel = level;
}

int ScheduleManager::GetNumberOfTaskletsCompletedLastRunWithTimeout()
{
	return s_numberOfTaskletsCompletedLastRunWithTimeout;
}

int ScheduleManager::GetNumberOfTaskletsSwitchedLastRunWithTimeout()
{
	return s_numberOfTaskletsSwitchedLastRunWithTimeout;
}


void ScheduleManager::RegisterTaskletToThread( Tasklet* tasklet )
{
	m_taskletsOnSchedulerThread.insert( tasklet );
}

void ScheduleManager::UnregisterTaskletFromThread( Tasklet* tasklet )
{
	if( m_taskletsOnSchedulerThread.find( tasklet ) != m_taskletsOnSchedulerThread.end() )
	{
		m_taskletsOnSchedulerThread.erase( tasklet );
	}
}

void ScheduleManager::ClearThreadTasklets()
{
    // Tasklets will be removed from being scheduled if their ScheduleManger is set to null
    // Tasklets without a schedule manager is an invalid state so Tasklets are killed if this is about to happen
	// It is possible that this becomes and infinate loop
	// For example if the python code catches the TaskletExit/generic exceptionscheduleManager->Decref(
	// as part of an inifinate loop
	// If this is the case carbon-scheduler will start to leak scheduleManagers and threads
	// This could be noticable through metrics
	// We could put a limit on the pumping of below and just instead opt to leak Tasklets/arguments

	auto taskletIter = m_taskletsOnSchedulerThread.begin();

	while( taskletIter != m_taskletsOnSchedulerThread.end() )
	{
		Tasklet* t = *taskletIter;

		// Disassociate tasklet from thread
		t->SetScheduleManager( nullptr );

        taskletIter = m_taskletsOnSchedulerThread.begin();
	}
}

unsigned long ScheduleManager::ThreadId() const
{
	return m_threadId;
}
