How exceptions are managed
==========================

Exceptions are supported and in most cases propagated so they can be handled when raised inside a :doc:`../pythonApi/tasklet`.

There is one exception :py:func:`scheduler.TaskletExit` which is handled slightly differently.


Standard Python Exception Raising internal to a :doc:`../pythonApi/tasklet`
----------------------------------------------------------------------------

Raising an exception from inside a :doc:`../pythonApi/tasklet` callable will propagate the exception to the caller.

.. code-block:: python

   def taskletRaisingAnException():
      raise RuntimeError

   t = scheduler.tasklet(taskletRaisingAnException)()

   scheduler.run()

   >>>Traceback (most recent call last):
   >>>File "<stdin>", line 1, in <module>
   >>>File "<stdin>", line 2, in foo
   >>>RuntimeError

Explanation of computation:
1. :doc:`../pythonApi/tasklet` ``t`` is created and run with :py:func:`scheduler.run`.
2. A ``RuntimeError`` is raised in the callable of :doc:`../pythonApi/tasklet` ``t``.
3. It is propagated to the caller, in this case the main :doc:`../pythonApi/tasklet`.


Raising an exception externally to a :doc:`../pythonApi/tasklet`
----------------------------------------------------------------

:py:func:`scheduler.tasklet.throw` or :py:func:`scheduler.tasklet.raise_exception` can be used to raise exceptions externally to a :doc:`../pythonApi/tasklet`. They can then be handled as usual.

.. code-block:: python

   def foo():
      try:
         scheduler.schedule()
      except RuntimeError:
         print("Runtime Error Raised in Tasklet")

   t = scheduler.tasklet(foo)()

   t.run()

   t.throw(RuntimeError)

   >>>Runtime Error Raised in Tasklet

Throw can also be set as ``pending`` which will cause the :doc:`../pythonApi/tasklet` to be inserted into the runnables queue rather than executed immediately.


Sending an exception over a :doc:`../pythonApi/channel`
-------------------------------------------------------

:py:func:`scheduler.channel.send_exception` or :py:func:`scheduler.channel.send_throw` can be used to send an exception over a :doc:`../pythonApi/channel`, causing the sent exception to be raised on the reciever.

.. code-block:: python

   c = scheduler.channel()

   def foo():
      try:
         c.receive()
      except RuntimeError:
         print("Runtime Error Raised in Tasklet")

   t = scheduler.tasklet(foo)()

   t.run()

   c.send_exception(RuntimeError)

   >>>Runtime Error Raised in Tasklet


Understanding the custom exception :py:func:`scheduler.TaskletExit`
-------------------------------------------------------------------

A special custom exception is provided :py:func:`scheduler.TaskletExit`.

Unlike standard exceptions, :py:func:`scheduler.TaskletExit` does not propagate outside the :doc:`../pythonApi/tasklet` it was raised on as illustrated with the example below.

.. code-block:: python

   def taskletRaisingAnException():
      raise scheduler.TaskletExit

   scheduler.tasklet(taskletRaisingAnException)()

   scheduler.run()


:py:func:`scheduler.TaskletExit` exceptions will be raised when :py:func:`scheduler.tasklet.kill` is called on an already running :doc:`../pythonApi/tasklet`. This allows the killed :doc:`../pythonApi/tasklet` to deal with possible ramifications of the kill.
For further information on kill refer to :doc:`killingTasklets`.

.. code-block:: python

   def foo():
      try:
         scheduler.schedule()
      except scheduler.TaskletExit:
         print("Kill called")

   scheduler.tasklet(foo)()

   t.run()

   t.kill()

   >>>Kill called

Preventing uncaught exceptions from being raised on parent tasklets using :py:attr:`scheduler.tasklet.dont_raise`
-----------------------------------------------------------------------------------------------------------------

Usually, (excluding TaskletExit exceptions) an uncaught exception on a child tasklet will result in that exception being raised on the parent tasklet. setting :py:attr:`scheduler.tasklet.dont_raise` to `True` before binding a tasklet will prevent this from happening.

.. code-block:: python

   def testMethod():
      raise TypeError("test")
      print("SHOULD NOT PRINT")

   t = scheduler.tasklet()
   t.dont_raise = True
   t.bind(testMethod)
   t.setup()

   scheduler.run()
   print("scheduler.run() call did not raise")

   >>>scheduler.run() call did not raise

Attempting to change the value of :py:attr:`scheduler.tasklet.dont_raise` after a taslket has been bound will result in a RuntimeError being raised.

.. code-block:: python

   def testMethod():
      pass

   t = scheduler.tasklet()
   t.dont_raise = True
   t.bind(testMethod)
   t.setup()

   >>>RuntimeError: You cannot change this value after the tasklet has been bound

You can set an exception handler (None by default) by setting :py:attr:`scheduler.tasklet.exception_handler` to a function that takes one argument, a string containing tasklet information:

.. code-block:: python
   
   def testMethod():
      raise TypeError("test")

   def exception_handler(infostring):
      print(infostring)
      import traceback
      traceback.print_exc()

   t = scheduler.tasklet()
   t.dont_raise = True
   t.context = "extra info"
   t.bind(testMethod)
   t.setup()
   t.exception_handler = exception_handler

   scheduler.run()

   >>>Unhandled exception in <Tasklet alive=1 blocked=0 paused=0 scheduled=1 context=extra info>
      Traceback (most recent call last):
        File "yourPythonCode.py", line 3, in testMethod
          raise TypeError("test")
      TypeError: test

Any exceptions that occur in the handler code will cause both exceptions to be written to stderr.


Suggested Further Reading
-------------------------

:doc:`killingTasklets`


