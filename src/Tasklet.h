/* 
	*************************************************************************

	Tasklet.h

	Author:    James Hawk
	Created:   Feb. 2024
	Project:   Scheduler

	Description:   

	  Functions related to tasklets

	(c) CCP 2024

	*************************************************************************
*/
#pragma once
#ifndef Tasklet_H
#define Tasklet_H

#include <string>

#include "stdafx.h"

#include "PythonCppType.h"

class Channel;
class ScheduleManager;
enum class ChannelDirection;

// Specify the technique used when rescheduling
// BACK: Insert Tasklet to the end of the current queue
// FRONT_PLUS_ONE: Inserts the Tasklet one from the front of the queue 
enum class RescheduleType
{
    FRONT_PLUS_ONE,
    BACK,
    NONE
};

class Tasklet : public PythonCppType
{
public:

	Tasklet( PyObject* pythonObject, PyObject* taskletExitException, bool isMain );

    ~Tasklet();

	void SetToCurrentGreenlet();

    bool Remove();

	bool Insert();

    bool SwitchImplementation();

    bool SwitchTo();

    bool Run();

    bool Kill( bool pending = false );

    PyObject* GetTransferArguments();

	void ClearTransferArguments();

	void SetTransferArguments( PyObject* args, PyObject* exception, bool restoreException );

    void Block( Channel* channel );

    void Unblock();

    bool IsBlocked() const;

    bool IsOnChannelBlockList() const;

    bool IsAlive() const;

    bool IsScheduled() const;

    void SetScheduled( bool value );

    bool IsBlocktrapped() const;

    void SetBlocktrap( bool value );

    bool IsMain() const;

    void MarkAsMain( bool value );

    unsigned long ThreadId() const;

	Tasklet* Next() const;

	void SetNext( Tasklet* next );

    Tasklet* Previous() const;

	void SetPrevious( Tasklet* previous );

    Tasklet* NextBlocked() const;

    void SetNextBlocked(Tasklet* next);

    Tasklet* PreviousBlocked() const;

    void SetPreviousBlocked( Tasklet* previous );

    PyObject* Arguments() const;

    PyObject* KwArguments() const;

    bool TransferInProgress() const;

    void SetTransferInProgress( bool value );

    PyObject* TransferException() const;

    bool ShouldRestoreTransferException() const;

    bool ThrowException( PyObject* exception, PyObject* value, PyObject* tb, bool pending );

    bool IsPaused();

    Tasklet* GetParent();

    bool SetParent( Tasklet* parent );

    bool TaskletExceptionRaised();

    void ClearTaskletException();

    void SetReschedule( RescheduleType value );

    RescheduleType RequiresReschedule();

    void SetTaggedForRemoval( bool value );

    PyObject* GetCallable();

    bool RequiresRemoval();

    ChannelDirection GetBlockedDirection();

    void SetBlockedDirection( ChannelDirection direction );

    void SetScheduleManager( ScheduleManager* scheduleManager );

    ScheduleManager* GetScheduleManager( );
   
    bool Setup( PyObject* args, PyObject* kwargs );

    bool Bind( PyObject* callable, PyObject* args, PyObject* kwargs );

    bool UnBind();

    void Clear();

    void SetMethodName(const std::string& methodName);

    const std::string& GetMethodName();

    void SetModuleName(const std::string& moduleName);

	const std::string& GetModuleName();

    void SetContext(const std::string& context );

    const std::string& GetContext();

    const std::string& GetFilename();

    void SetFilename(const std::string& fileName );

    long GetLineNumber();

    void SetLineNumber( long lineNumber );

    const std::string& GetParentCallsite();

    void SetParentCallsite(const std::string& parentCallsite );

    long long GetStartTime();
	
    void SetStartTime( long long startTime );

    long long GetEndTime();

    void SetEndTime( long long startTime );

    double GetRunTime();

    void SetRunTime( double runTime );

    bool GetHighlighted();

    void SetHighlighted( bool highlighted );

    static long GetAllTimeTaskletCount();

    static long GetActiveTaskletCount();

    void OnCallableEntered();

    void OnCallableExited();

    bool SetCallsiteData( PyObject* callable );

    bool GetDontRaise() const;

    bool SetDontRaise(bool dontRaise);

    PyObject* GetContextManagerCallable() const;

    void SetContextManagerCallable( PyObject* contextManagerCallable );

    long GetTimesSwitchedTo();

    PyObject* GetExceptionHandler() const;

    void SetExceptionHandler( PyObject* exceptionHandler );

private:

    void SetExceptionState( PyObject* exception, PyObject* arguments = Py_None );

	void SetPythonExceptionStateFromTaskletExceptionState();

    void ClearException();

    void SetAlive( bool value );

    void SetCallable( PyObject* callable );

    void SetArguments( PyObject* arguments );

    void SetKwArguments( PyObject* kwarguments );

    bool Initialise();

	void Uninitialise();

    bool BelongsToCurrentThread();

private:

	PyGreenlet* m_greenlet;

	PyObject* m_callable;

	PyObject* m_arguments;

    PyObject* m_kwArguments;

    bool m_isMain;

    bool m_transferInProgress;;

    bool m_scheduled;

	bool m_alive;

    bool m_blocktrap;

    Tasklet* m_previous;

    Tasklet* m_next;

    Tasklet* m_nextBlocked;

	Tasklet* m_previousBlocked;

    unsigned long m_threadId;

    PyObject* m_transferArguments;

    PyObject* m_transferException;

	bool m_restoreException;

    Channel* m_channelBlockedOn;

	bool m_blocked;

	ChannelDirection m_blockedDirection;

    bool m_paused;

    bool m_firstRun;

    long m_timesSwitchedTo;

    RescheduleType m_reschedule;

    bool m_remove;

    bool m_taggedForRemoval;  // This flag set will ensure that the tasklet doesn't get marked as not alive

    Tasklet* m_taskletParent; // Weak ref

    //Exception
    PyObject* m_exceptionState;
	PyObject* m_exceptionArguments;

    PyObject* m_taskletExitException; //Weak ref

    PyObject* m_exceptionHandler;

    ScheduleManager* m_scheduleManager;

    PyObject* m_ContextManagerCallable;

    bool m_killPending;

    std::string m_parentCallsite;
    std::string m_methodName;
    std::string m_moduleName;
    std::string m_context;
    std::string m_fileName;
    long m_lineNumber;
    long long m_startTime;
    long long m_endTime;
    double m_runTime;
    bool m_highlighted;
    inline static long s_totalAllTimeTaskletCount = 0;

    inline static long s_totalActiveTasklets = 0;

    bool m_dontRaise;
};

#endif // Tasklet_H