#include "Channel.h"

#include <vector>

#include "Tasklet.h"
#include "ScheduleManager.h"


Channel::Channel( PyObject* pythonObject ) :
	PythonCppType( pythonObject ),
	m_balance(0),
	m_preference(ChannelPreference::RECEIVER),
	m_firstBlockedOnSend( nullptr ),
	m_lastBlockedOnSend( nullptr ),
	m_firstBlockedOnReceive( nullptr ),
	m_lastBlockedOnReceive( nullptr ),
	m_closing( false ),
	m_closed( false )
{
    // Store weak reference in central store
    // Required just in case we lose all references to channel
    // The module will then be able to unblock if needed
    s_activeChannels.insert( this );
}

Channel::~Channel()
{
	// Note: Destructor will never be called while there are tasklets blocking

	// Remove weak ref from store
	s_activeChannels.erase( this );
}

bool Channel::Send( PyObject* args, PyObject* exception /* = nullptr */, bool restoreException /* = false */)
{
    ScheduleManager* scheduleManager = ScheduleManager::GetThreadScheduleManager();

    Tasklet* current = scheduleManager->GetCurrentTasklet();

	RunChannelCallback( this, current, true, m_lastBlockedOnReceive == nullptr );

    current->SetTransferInProgress(true);

	if( m_firstBlockedOnReceive == nullptr )
	{
		// Block as there is no tasklet sending
        if( !current )
		{
			PyErr_SetString( PyExc_RuntimeError, "No current tasklet set" );

			return false;
		}

        //If current tasklet has block_trap set to true then throw runtime error
		if( current->IsBlocktrapped())
		{
			PyErr_SetString( PyExc_RuntimeError, "Channel cannot block on main tasklet with block_trap set true" );

			return false;
		}

        // Ensure channel is open
        if (m_closed || m_closing)
        {
			PyErr_SetString( PyExc_ValueError, "Send operation on a closed channel" );

			return false;
        }

		// Block as there is no tasklet receiving
		current->Incref();

        AddTaskletToWaitingToSend( current );

		current->Block( this );

        UpdateCloseState();

        current->SetTransferArguments( args, exception, restoreException );

         // Continue scheduler
		if( !scheduleManager->Yield() )
		{
			current->SetTransferInProgress( false );

            RemoveTaskletFromBlocked( current );

            auto transferArguments = current->GetTransferArguments();

            if ( transferArguments )
            {
				// If branch is entered it must mean that the tasklet was killed/error raised
				// before the transfer completed
				// Transfer arguments will not have been cleared, so we need to clean them up
				// The tasklet reference belonged to the channel as it was in the block list
				Py_DecRef( transferArguments );
				current->ClearTransferArguments();
				current->Decref();
            }

            current->Unblock();

            UpdateCloseState();

			return false;
        }

    }
    else
    {
		Tasklet* receivingTasklet = PopNextTaskletBlockedOnReceive();

		receivingTasklet->Unblock();

		// Store for retrieval from receiving tasklet
		receivingTasklet->SetTransferArguments( args, exception, restoreException );

		Tasklet* current_tasklet = scheduleManager->GetCurrentTasklet();

		UpdateCloseState();

		if( m_preference == ChannelPreference::RECEIVER )
		{
			receivingTasklet->GetScheduleManager()->InsertTaskletToRunNext( receivingTasklet );
			receivingTasklet->Decref();
			if( !scheduleManager->Schedule( RescheduleType::BACK ) )
			{
				UpdateCloseState();
				return false;
			}
		}
		else
		{
			receivingTasklet->GetScheduleManager()->InsertTasklet( receivingTasklet );
			receivingTasklet->Decref();
		}
    }

	current->SetTransferInProgress( false );

    UpdateCloseState();

	return true;

}

PyObject* Channel::Receive()
{
    ScheduleManager* scheduleManager = ScheduleManager::GetThreadScheduleManager();

    scheduleManager->GetCurrentTasklet()->SetTransferInProgress( true );

	// Block as there is no tasklet sending
	Tasklet* current = scheduleManager->GetCurrentTasklet();

	RunChannelCallback( this , current, false, m_lastBlockedOnSend == nullptr );

    if( current == nullptr )
	{
		PyErr_SetString( PyExc_RuntimeError, "No current tasklet set" );

		return nullptr;
	}

    if( m_firstBlockedOnSend == nullptr )
	{
		current->Incref();
		AddTaskletToWaitingToReceive( current );
		//If current tasklet has block_trap set to true then throw runtime error
		if( current->IsBlocktrapped() )
		{
			RemoveTaskletFromBlocked( current );

            current->SetTransferInProgress( false );

			PyErr_SetString( PyExc_RuntimeError, "Channel cannot block on main tasklet with block_trap set true" );

            current->Decref();

			return nullptr;
		}

        // Ensure channel is open
        if( m_closed || m_closing )
		{

            RemoveTaskletFromBlocked( current );

            current->SetTransferInProgress( false );

			PyErr_SetString( PyExc_ValueError, "receive operation on a closed channel" );

			return nullptr;
		}
		
		current->Block( this );

        UpdateCloseState();

		// Continue scheduler
		if( !scheduleManager->Yield() )
		// Will enter here if an exception has been thrown on a tasklet
		{
			RemoveTaskletFromBlocked( current );

			current->Unblock();

			current->SetTransferInProgress( false );

            PyObject* transferArguments = current->GetTransferArguments();

            if (transferArguments)
            {
                // If branch is entered it must mean the transfer was complete
                // That is that the send has already set the transfer arguments
                // This then must be the final call to finish receive
                // If a kill or error has been raised on the tasklet before it
                // Is processed on the queue then we need to clean up the transfer arguments
                // We also don't need to decref the tasklet, the ref is held by the scheduler queue
                // This tasklet is not technically blocked at this point
				Py_DecRef( transferArguments );
				current->ClearTransferArguments();
            }
            else
            {
                // If branch is entered it must mean that the tasklet was killed/error raised
                // before the transfer completed
                // No transfer arguments will have been set so no need to clean them up
                // The tasklet reference belonged to the channel as it was in the block list
				current->Decref();
            }

            UpdateCloseState();

			return nullptr;
		}
	}
	else
	{
		Tasklet* sendingTasklet = PopNextTaskletBlockedOnSend();
		sendingTasklet->Unblock();
		sendingTasklet->SetTransferInProgress( false );

        current->SetTransferArguments(
            sendingTasklet->GetTransferArguments(),
            sendingTasklet->TransferException(), 
            sendingTasklet->ShouldRestoreTransferException()
        );

        Py_DECREF( sendingTasklet->GetTransferArguments() );
        sendingTasklet->ClearTransferArguments();

        UpdateCloseState();
        
        if (m_preference == ChannelPreference::SENDER)
        {
			sendingTasklet->GetScheduleManager()->InsertTaskletToRunNext( sendingTasklet );
			sendingTasklet->Decref();
			if( !scheduleManager->Schedule( RescheduleType::BACK ) )
            {
				current->Decref();
				UpdateCloseState();
				return nullptr;
            }
        }
        else
        {
			sendingTasklet->GetScheduleManager()->InsertTasklet(sendingTasklet);
			sendingTasklet->Decref();
        }
	}

    
    //Process the exception
	PyObject* transferException = current->TransferException();

	if( transferException )
	{	
        PyObject* arguments = current->GetTransferArguments();

        // If arguments are Py_None, then we want to use the exception data as it is set in send_throw
        if (current->ShouldRestoreTransferException())
        {
            auto exceptionType = PyTuple_GetItem( transferException, 0 );
			auto exceptionValue = PyTuple_GetItem( transferException, 1 );
			auto exceptionTb = PyTuple_GetItem( transferException, 2 );

            Py_INCREF( exceptionType );
			Py_INCREF( exceptionValue );
			Py_INCREF( exceptionTb );

            Py_DECREF( transferException );
            
			PyErr_Restore( exceptionType, exceptionValue == Py_None ? nullptr : exceptionValue, exceptionTb == Py_None ? nullptr : exceptionTb );
        }
        else
        {
			PyErr_SetObject( transferException, arguments );

			Py_DecRef( transferException );

        }

        Py_DecRef( arguments );

        current->ClearTransferArguments();

		current->SetTransferInProgress( false );

        UpdateCloseState();
       
        return nullptr;

    }

	current->SetTransferInProgress( false );

	auto ret = current->GetTransferArguments();

	current->ClearTransferArguments();

	return ret;
}

int Channel::Balance() const
{
	return m_balance;
}

void Channel::UnblockTaskletFromChannel( Tasklet* tasklet )
{
    // Public exposed remove_tasklet_from_blocked wrapped in lock for thread safety
    RemoveTaskletFromBlocked( tasklet );
}

void Channel::RemoveTaskletFromBlocked( Tasklet* tasklet )
{
	bool endNode = false;
    if (tasklet == m_lastBlockedOnReceive)
    {
		m_lastBlockedOnReceive = tasklet->NextBlocked();
        if (m_lastBlockedOnReceive != nullptr)
        {
			m_lastBlockedOnReceive->SetPreviousBlocked( nullptr );
        }

		endNode = true;
    }

    if (tasklet == m_lastBlockedOnSend)
    {
		m_lastBlockedOnSend = tasklet->NextBlocked();
        if (m_lastBlockedOnSend != nullptr)
        {
			m_lastBlockedOnSend->SetPreviousBlocked( nullptr );
        }

        endNode = true;
    }

    if (tasklet == m_firstBlockedOnReceive)
    {
		m_firstBlockedOnReceive = tasklet->PreviousBlocked();
        if (m_firstBlockedOnReceive != nullptr)
        {
			m_firstBlockedOnReceive->SetNextBlocked( nullptr );
        }
		
		endNode = true;
    }

    if (tasklet == m_firstBlockedOnSend)
    {
		m_firstBlockedOnSend = tasklet->PreviousBlocked();
        if (m_firstBlockedOnSend != nullptr)
        {
			m_firstBlockedOnSend->SetNextBlocked( nullptr );
        }
		
		endNode = true;
    }

    if (!endNode)
    {
        if (tasklet->PreviousBlocked())
        {
			tasklet->PreviousBlocked()->SetNextBlocked( tasklet->NextBlocked() );
        }
        if (tasklet->NextBlocked())
        {
			tasklet->NextBlocked()->SetPreviousBlocked( tasklet->PreviousBlocked() );
        }
    }

    tasklet->SetNextBlocked( nullptr );
	tasklet->SetPreviousBlocked( nullptr );

    if (tasklet->GetBlockedDirection() == ChannelDirection::SENDER)
    {
		DecrementBalance();
    }
	else if( tasklet->GetBlockedDirection() == ChannelDirection ::RECEIVER)
    {
		IncrementBalance();
    }

    tasklet->SetBlockedDirection( ChannelDirection::NEITHER );
    
}

void Channel::RunChannelCallback( Channel* channel, Tasklet* tasklet, bool sending, bool willBlock ) const
{
	if( s_channelCallback )
	{
		PyObject* args = PyTuple_New( 4 );

		channel->Incref();

		PyTuple_SetItem( args, 0, channel->PythonObject() );

		tasklet->Incref();

		PyTuple_SetItem( args, 1, tasklet->PythonObject() );

		PyTuple_SetItem( args, 2, sending ? Py_True : Py_False );

		PyTuple_SetItem( args, 3, willBlock ? Py_True : Py_False );

		PyObject_Call( s_channelCallback, args, nullptr );

		Py_DecRef( args );
	}
}

void Channel::AddTaskletToWaitingToSend( Tasklet* tasklet )
{
    if( m_lastBlockedOnSend == nullptr )
    {
		m_lastBlockedOnSend = tasklet;
		m_firstBlockedOnSend = tasklet;
    }
	else
	{
		m_lastBlockedOnSend->SetPreviousBlocked( tasklet );
		tasklet->SetNextBlocked( m_lastBlockedOnSend );
		m_lastBlockedOnSend = tasklet;
    }

    tasklet->SetBlockedDirection( ChannelDirection::SENDER );
    IncrementBalance();
}

void Channel::AddTaskletToWaitingToReceive( Tasklet* tasklet )
{
	if( m_lastBlockedOnReceive == nullptr )
    {
		m_lastBlockedOnReceive = tasklet;
        m_firstBlockedOnReceive = tasklet;
    }
    else
    {
		m_lastBlockedOnReceive->SetPreviousBlocked( tasklet );
		tasklet->SetNextBlocked( m_lastBlockedOnReceive );
		m_lastBlockedOnReceive = tasklet;
    }

    tasklet->SetBlockedDirection( ChannelDirection::RECEIVER );
    DecrementBalance();
}

Tasklet* Channel::PopNextTaskletBlockedOnSend()
{
	Tasklet* next = nullptr;
    if (m_firstBlockedOnSend != nullptr)
    {
		next = m_firstBlockedOnSend;

		RemoveTaskletFromBlocked( next );
    }
	
    return next;
}

Tasklet* Channel::PopNextTaskletBlockedOnReceive()
{
	Tasklet* next = nullptr;
    if (m_firstBlockedOnReceive != nullptr)
    {
		next = m_firstBlockedOnReceive;

		RemoveTaskletFromBlocked( next );
    }

    return next;
}

PyObject* Channel::ChannelCallback()
{
	return s_channelCallback;
}

void Channel::SetChannelCallback( PyObject* callback )
{
	Py_XDECREF( s_channelCallback ); 

    s_channelCallback = callback;
}

int Channel::PreferenceAsInt() const
{
	return DirectionToInt( m_preference );
}

void Channel::SetPreferenceFromInt( int value )
{
	m_preference = DirectionFromInt( value );
}

Tasklet* Channel::BlockedQueueFront() const
{
	if( m_firstBlockedOnReceive != nullptr )
    {
		return m_firstBlockedOnReceive;
    }
    else if (m_firstBlockedOnSend != nullptr)
    {
		return m_firstBlockedOnSend;
    }
	return nullptr;
}

void Channel::ClearBlocked( bool pending )
{
	// Kill all blocked tasklets
	while( m_firstBlockedOnReceive )
	{
		m_firstBlockedOnReceive->Kill( pending );
	}

	while( m_firstBlockedOnSend )
	{
		m_firstBlockedOnSend->Kill( pending );
	}

}

long Channel::NumberOfActiveChannels()
{
	return static_cast<long>(s_activeChannels.size());
}

int Channel::UnblockAllActiveChannels()
{
	int numberOfChannelsUnblocked = 0;

	auto iter = s_activeChannels.begin();

	std::vector<Channel*> channelsToUnblock;

	while(iter != s_activeChannels.end())
	{
		Channel* channel = *iter;
		if (channel->m_balance != 0)
		{
			channelsToUnblock.push_back(channel);
		}
		iter++;
	}

	for (auto chan : channelsToUnblock)
	{
		numberOfChannelsUnblocked++;

		chan->ClearBlocked( false );
	}

    return numberOfChannelsUnblocked;
}

void Channel::Close()
{
	m_closing = true;

    UpdateCloseState();
}

void Channel::Open()
{
	m_closing = false;

	m_closed = false;
}

bool Channel::IsClosed() const
{
	return m_closed;
}

bool Channel::IsClosing() const
{
	return m_closing;
}

void Channel::IncrementBalance()
{
	m_balance++;
}

void Channel::DecrementBalance()
{
	m_balance--;
}

void Channel::UpdateCloseState()
{
    // If channel is set to close and the balance is zero then set as closed

	if((m_closing) && ( m_balance == 0 ))
	{

		m_closed = true;

	}
}

int Channel::DirectionToInt( ChannelDirection preference ) const
{
    switch (preference)
	{
	
        case ChannelDirection::RECEIVER:
		    return -1;

        case ChannelDirection::NEITHER:
            return 0;

        case ChannelDirection::SENDER:
			return 1;

        default:
			return -1;

    }
}

ChannelDirection Channel::DirectionFromInt( int preference ) const
{
	switch( preference )
	{
        case -1: 
	        return ChannelDirection::RECEIVER;

        case 0:
			return ChannelDirection::NEITHER;

        case 1:
			return ChannelDirection::SENDER;

        default:
			return ChannelDirection::RECEIVER;
	}
}

ChannelDirection Channel::InvertDirection( ChannelDirection direction ) const
{
	switch( direction )
	{
	case ChannelDirection::RECEIVER:
		return ChannelDirection::SENDER;

	case ChannelDirection::SENDER:
		return ChannelDirection::RECEIVER;

	case ChannelDirection::NEITHER:
		return ChannelDirection::NEITHER;

    default:
		return ChannelDirection::NEITHER;
	}
}
