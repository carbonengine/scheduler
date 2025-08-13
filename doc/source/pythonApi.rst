Python API
==========


Refer to :doc:`guides` for further usage information.

Custom Types
------------

.. toctree::
   :maxdepth: 1

   pythonApi/channel

   pythonApi/tasklet

   pythonApi/scheduleManager


Exceptions
----------

.. autoexception:: scheduler.TaskletExit

   Tasklet exception handled internally to Tasklet functioning

   Refer to :doc:`guides/howExceptionsAreManaged` for further details.


Methods
-------

.. autofunction:: scheduler.set_channel_callback
   
   Callback signature is expect to be of the form:

   .. code-block:: python

      def callback(channel, tasklet, is_sending, will_block):

   **Where**

      ``channel`` refers to the Channel performing the switch

      ``nextTasklet`` refers to the Tasklet performing the action on the Channel

      ``sending`` is an integer where a non-zero value indicates the channel action ia a send rather than a receive.

      ``willblock`` refers to an integer where a non-zero value indicates the channel action will result in the tasklet being blocked on the Channel.

   :seealso: :py:func:`scheduler.get_channel_callback`

.. autofunction:: scheduler.get_channel_callback

   :seealso: :py:func:`scheduler.set_channel_callback`

.. autofunction:: scheduler.enable_softswitch

.. autofunction:: scheduler.getcurrent

.. autofunction:: scheduler.getmain

.. autofunction:: scheduler.getruncount

   :seealso: :py:func:`scheduler.calculateruncount`

.. autofunction:: scheduler.calculateruncount

   :seealso: :py:func:`scheduler.getruncount`

.. autofunction:: scheduler.schedule

   For further information see :doc:`guides/understandingTaskletScheduleOrder`.

.. autofunction:: scheduler.schedule_remove

.. autofunction:: scheduler.run

   For further information see :doc:`guides/understandingTaskletScheduleOrder`.

.. autofunction:: scheduler.run_n_tasklets

   :seealso: :py:func:`scheduler.run`

.. autofunction:: scheduler.set_schedule_callback

   Callback signature is expect to be of the form:

   .. code-block:: python

      def callback(previousTasklet, nextTasklet):

   **Where**

      ``previousTasklet`` refers to the Tasklet swiched from (or None)

      ``nextTasklet`` refers to the Tasklet swiched to

   :seealso: :py:func:`scheduler.get_schedule_callback`

.. autofunction:: scheduler.get_schedule_callback

   :seealso: :py:func:`scheduler.set_schedule_callback`

.. autofunction:: scheduler.get_thread_info

.. autofunction:: scheduler.switch_trap

   For further information see :doc:`guides/restrictingTaskletControlFlow`.

.. autofunction:: scheduler.get_schedule_manager

   :seealso: :py:func:`scheduler.schedule_manager`

.. autofunction:: scheduler.get_number_of_active_schedule_managers

   :seealso: :py:func:`scheduler.schedule_manager`

.. autofunction:: scheduler.get_number_of_active_channels

   :seealso: :py:func:`scheduler.channel`

.. autofunction:: scheduler.unblock_all_channels

   :seealso: :py:func:`scheduler.channel`

.. autofunction:: scheduler.set_use_nested_tasklets

   For further information see :doc:`designDocuments/nestedTaskletsVsFlatSchedulingQueue`.

.. autofunction:: scheduler.get_use_nested_tasklets

   For further information see :doc:`designDocuments/nestedTaskletsVsFlatSchedulingQueue`.

.. autofunction:: scheduler.get_all_time_tasklet_count

.. autofunction:: scheduler.get_active_tasklet_count