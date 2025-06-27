import sys
from test_utils import SchedulerTestCaseBase
import scheduler


class TestChannels(SchedulerTestCaseBase):
    def test_blocking_send(self):
        ''' Test that when a tasklet sends to a channel without waiting receivers, the tasklet is blocked. '''

        # Function to block when run in a tasklet.
        def f(testChannel):
            testChannel.send(1)

        # Get the tasklet blocked on the channel.
        channel = scheduler.channel()
        tasklet = scheduler.tasklet(f)(channel)
        self.assertEqual(self.getruncount(), 2)
        tasklet.run()
        self.assertEqual(self.getruncount(), 1)

        # The tasklet should be blocked.
        self.assertTrue(tasklet.blocked, "The tasklet should have been run and have blocked on the channel waiting for a corresponding receiver")

        # The channel should have a balance indicating one blocked sender.
        self.assertTrue(channel.balance == 1, "The channel balance should indicate one blocked sender waiting for a corresponding receiver")

    def test_blocking_receive(self):
        ''' Test that when a tasklet receives from a channel without waiting senders, the tasklet is blocked. '''

        # Function to block when run in a tasklet.
        def f(testChannel):
            testChannel.receive()

        # Get the tasklet blocked on the channel.
        channel = scheduler.channel()
        tasklet = scheduler.tasklet(f)(channel)
        self.assertEqual(self.getruncount(), 2)
        tasklet.run()
        self.assertEqual(self.getruncount(), 1)

        # The tasklet should be blocked.
        self.assertTrue(tasklet.blocked, "The tasklet should have been run and have blocked on the channel waiting for a corresponding sender")

        # The channel should have a balance indicating one blocked sender.
        self.assertEqual(channel.balance, -1, "The channel balance should indicate one blocked receiver waiting for a corresponding sender")

    def test_non_blocking_send(self):
        ''' Test that when there is a waiting receiver, we can send without blocking with normal channel behaviour. '''

        originalValue = 1
        receivedValues = []

        # Function to block when run in a tasklet.
        def f(testChannel):
            receivedValues.append(testChannel.receive())

        # Get the tasklet blocked on the channel.
        channel = scheduler.channel()
        tasklet = scheduler.tasklet(f)(channel)
        self.assertEqual(self.getruncount(), 2)
        tasklet.run()
        self.assertEqual(self.getruncount(), 1)

        # Make sure that the current tasklet cannot block when it tries to receive.  We do not want
        # to exit this test having clobbered the block trapping value, so we make sure we restore
        # it.
        oldBlockTrap = scheduler.getcurrent().block_trap
        try:
            scheduler.getcurrent().block_trap = True
            channel.send(originalValue)
        finally:
            scheduler.getcurrent().block_trap = oldBlockTrap

        self.assertTrue(len(receivedValues) == 1 and receivedValues[0] == originalValue, "We sent a value, but it was not the one we received.  Completely unexpected.")

    def test_non_blocking_receive(self):
        ''' Test that when there is a waiting sender, we can receive without blocking with normal channel behaviour. '''
        originalValue = 1

        # Function to block when run in a tasklet.
        def f(testChannel, valueToSend):
            testChannel.send(valueToSend)

        # Get the tasklet blocked on the channel.
        channel = scheduler.channel()
        tasklet = scheduler.tasklet(f)(channel, originalValue)
        self.assertEqual(self.getruncount(), 2)
        tasklet.run()
        self.assertEqual(self.getruncount(), 1)

        # Make sure that the current tasklet cannot block when it tries to receive.  We do not want
        # to exit this test having clobbered the block trapping value, so we make sure we restore
        # it.
        oldBlockTrap = scheduler.getcurrent().block_trap
        try:
            scheduler.getcurrent().block_trap = True
            value = channel.receive()
        finally:
            scheduler.getcurrent().block_trap = oldBlockTrap

        tasklet.kill()

        self.assertEqual(value, originalValue, "We received a value, but it was not the one we sent.  Completely unexpected.")

    def test_block_trap_send(self):
        '''Test that block trapping works when receiving'''
        channel = scheduler.channel()
        count = [0]


        def f():
            with scheduler.block_trap():
                self.assertRaises(RuntimeError, channel.send, None)
            count[0] += 1

        # Test on main tasklet and on worker
        f()
        scheduler.tasklet(f)()
        self.assertEqual(self.getruncount(), 2)
        scheduler.run()
        self.assertEqual(self.getruncount(), 1)
        self.assertEqual(count[0], 2)

    def test_block_trap_recv(self):
        '''Test that block trapping works when receiving'''
        channel = scheduler.channel()
        count = [0]

        def f():
            with scheduler.block_trap():
                self.assertRaises(RuntimeError, channel.receive)
            count[0] += 1

        f()
        scheduler.tasklet(f)()
        self.assertEqual(self.getruncount(), 2)
        scheduler.run()
        self.assertEqual(self.getruncount(), 1)
        self.assertEqual(count[0], 2)

    def test_main_tasklet_blocking_without_a_sender(self):
        ''' Test that the last runnable tasklet cannot be blocked on a channel receive. '''
        c = scheduler.channel()
        self.assertRaises(RuntimeError, c.receive)

    def test_main_tasklet_blocking_without_receiver(self):
        ''' Test that the last runnable tasklet cannot be blocked on a channel send. '''
        c = scheduler.channel()
        def test_send():
            c.send(1)

        self.assertRaises(RuntimeError, test_send)

    def test_main_tasklet_receive_deadlock_after_running_child_tasklets(self):
        runOrder = []

        def noop(i):
            runOrder.append(i)

        for i in range(10):
            scheduler.tasklet(noop)(i)

        chan = scheduler.channel()
        self.assertRaisesRegex(RuntimeError, "Deadlock", chan.receive)
        self.assertEqual([0,1,2,3,4,5,6,7,8,9], runOrder)

    def test_main_tasklet_send_deadlock_after_running_child_tasklets(self):
        runOrder = []

        def noop(i):
            runOrder.append(i)

        for i in range(10):
            scheduler.tasklet(noop)(i)

        def send():
            chan.send(1)

        chan = scheduler.channel()
        self.assertRaisesRegex(RuntimeError, "Deadlock", send)
        self.assertEqual([0,1,2,3,4,5,6,7,8,9], runOrder)

    def test_inter_thread_communication(self):
        ''' Test that tasklets in different threads sending over channels to each other work. '''
        import threading
        commandChannel = scheduler.channel()

        def master_func():
            commandChannel.send("ECHO 1")
            commandChannel.send("ECHO 2")
            commandChannel.send("ECHO 3")
            commandChannel.send("QUIT")

        def slave_func():
            while 1:
                command = commandChannel.receive()
                if command == "QUIT":
                    break

        def scheduler_run(tasklet_func):
            t = scheduler.tasklet(tasklet_func)()
            while t.alive:
                scheduler.run()

        thread = threading.Thread(target=scheduler_run, args=(master_func,))
        thread.start()

        scheduler_run(slave_func)
        thread.join()

    def test_sending_tasklets_rescheduled_by_channel_are_run(self):
        run_order = []

        def sender(chan, x):
          chan.send(x)
          run_order.append(2)

        def receiver(chan):
          r = chan.receive()
          run_order.append(1)

        channel = scheduler.channel()
        _tasklet_A = scheduler.tasklet(receiver)(channel)
        _tasklet_B = scheduler.tasklet(sender)(channel, "Joe")

        scheduler.run()

        self.assertEqual(run_order, [1,2])

        run_order = []

        def sender(chan, x):
          chan.send(x)
          run_order.append(2)

        def receiver(chan):
          r = chan.receive()
          run_order.append(1)

        channel = scheduler.channel()
        _tasklet_B = scheduler.tasklet(sender)(channel, "Joe")
        _tasklet_A = scheduler.tasklet(receiver)(channel)

        scheduler.run()

        self.assertEqual(run_order, [1,2])

    def test_receiving_tasklets_rescheduled_by_channel_are_run(self):
        run_order = []

        def sender(chan, x):
          chan.send(x)
          run_order.append(1)

        def receiver(chan):
          r = chan.receive()
          run_order.append(2)

        channel = scheduler.channel()
        channel.preference = 1
        _tasklet_A = scheduler.tasklet(receiver)(channel)
        _tasklet_B = scheduler.tasklet(sender)(channel, "Joe")

        scheduler.run()

        self.assertEqual(run_order, [1,2])

        run_order = []

        def sender(chan, x):
          chan.send(x)
          run_order.append(1)

        def receiver(chan):
          r = chan.receive()
          run_order.append(2)

        channel = scheduler.channel()
        channel.preference = 1
        _tasklet_B = scheduler.tasklet(sender)(channel, "Joe")
        _tasklet_A = scheduler.tasklet(receiver)(channel)

        scheduler.run()

        self.assertEqual(run_order, [1,2])

    def test_send_exception(self):

        # Function to send the exception
        def f(testChannel):
            testChannel.send_exception(ValueError, 1, 2, 3)

        # Get the tasklet blocked on the channel.
        channel = scheduler.channel()
        tasklet = scheduler.tasklet(f)(channel)
        self.assertEqual(self.getruncount(), 2)
        tasklet.run()
        self.assertEqual(self.getruncount(), 1)
        self.assertRaises(ValueError, channel.receive)
        tasklet = scheduler.tasklet(f)(channel)
        self.assertEqual(self.getruncount(), 3)
        tasklet.run()
        self.assertEqual(self.getruncount(), 2)
        try:
            channel.receive()
        except ValueError as e:
            self.assertEqual(e.args, (1, 2, 3))

    def test_send_throw_prefence_send(self):
        import traceback

        channel = scheduler.channel()
        channel.preference = 1

        run = [False]

        def bar():
            raise ValueError(1, 2, 3)

        def blocker(c):
            try:
                c.receive()
            except ValueError:
                _, val, tb = sys.exc_info()
                self.assertEqual(val.args, (1, 2, 3))

                # Check that the traceback is correct
                l = traceback.extract_tb(tb)
                self.assertEqual(l[-1][2], "bar")
                run[0] = True
            
        t = scheduler.tasklet(blocker)(channel)
        t.run()

        try:
            bar()
        except:
            channel.send_throw(*sys.exc_info())

        scheduler.run()

        self.assertTrue(run[0])

    def test_send_throw(self):
        import traceback

        # subfunction in tasklet
        def bar():
            raise ValueError(1, 2, 3)

        # Function to send the exception
        def f(testChannel):
            try:
                bar()
            except Exception:
                testChannel.send_throw(*sys.exc_info())

        # Get the tasklet blocked on the channel.
        channel = scheduler.channel()
        channel.preference = -1
        tasklet = scheduler.tasklet(f)(channel)
        self.assertEqual(self.getruncount(), 2)
        tasklet.run()
        self.assertEqual(self.getruncount(), 1)
        self.assertRaises(ValueError, channel.receive)

        tasklet = scheduler.tasklet(f)(channel)
        self.assertEqual(self.getruncount(), 3)
        tasklet.run()
        self.assertEqual(self.getruncount(), 2)
        try:
            channel.receive()
        except ValueError:
            _, val, tb = sys.exc_info()
            self.assertEqual(val.args, (1, 2, 3))

            # Check that the traceback is correct
            l = traceback.extract_tb(tb)
            self.assertEqual(l[-1][2], "bar")

    def test_blocking_receive_on_main_tasklet(self):

        sentValues = []

        def sender(chan):
            for i in range(0, 10):
                chan.send(i)
                sentValues.append(i)

        channel = scheduler.channel()

        sendingTasklet = scheduler.tasklet(sender)(channel)
        self.assertEqual(self.getruncount(), 2)
        sendingTasklet.run()
        self.assertEqual(self.getruncount(), 1)

        self.assertEqual(len(sentValues), 0)
        self.assertEqual(channel.balance, 1)

        for i in range(0, 10):
            r = channel.receive()

        self.assertEqual(channel.balance, 0)
        scheduler.run()
        self.assertEqual(sentValues, [0,1,2,3,4,5,6,7,8,9])

    def test_blocked_tasklets_greenlet_is_not_parent(self):
        taskletOrder = []
        def foo(x):
            taskletOrder.append(x)

        channel = scheduler.channel()

        def sender(chan):
            scheduler.tasklet(foo)("a")
            chan.send(1)
            scheduler.tasklet(foo)("b")
            chan.send(2)
            scheduler.tasklet(foo)("c")
            chan.send(3)
            scheduler.tasklet(foo)("d")

        senderTasklet = scheduler.tasklet(sender)(channel)
        self.assertEqual(self.getruncount(), 2)
        senderTasklet.run()
        self.assertEqual(self.getruncount(), 2)

        # sendingTasklet
        r = channel.receive()
        taskletOrder.append(r)
        r = channel.receive()
        taskletOrder.append(r)
        r = channel.receive()
        taskletOrder.append(r)

        self.assertEqual(taskletOrder, [1, 'a', 2, 'b', 3])


    def test_blocking_send_on_main_tasklet(self):

        receivedValues = []

        def receiver(chan):
            for i in range(0, 10):
                r = chan.receive()
                receivedValues.append(r)

        channel = scheduler.channel()

        sendingTasklet = scheduler.tasklet(receiver)(channel)
        self.assertEqual(self.getruncount(), 2)
        sendingTasklet.run()
        self.assertEqual(self.getruncount(), 1)

        self.assertEqual(len(receivedValues), 0)
        self.assertEqual(channel.balance, -1)

        for i in range(0, 10):
            channel.send(i)

        self.assertEqual(channel.balance, 0)
        self.assertEqual(receivedValues, [0,1,2,3,4,5,6,7,8,9])

    def test_preference_sender(self):
        completedTasklets = []

        c = scheduler.channel()

        c.preference = 1

        def sender(chan, x):
            chan.send("test")
            completedTasklets.append(("sender", x))

        def receiver(chan, x):
            res = chan.receive()
            completedTasklets.append(("receiver", x))

        expectedExecutionOrder = [('sender', 0), ('sender', 1), ('sender', 2), ('receiver', 0), ('receiver', 1), ('receiver', 2)]

        for i in range(3):
            scheduler.tasklet(sender)(c, i)

        for i in range(3):
            scheduler.tasklet(receiver)(c, i)

        self.assertEqual(completedTasklets, [])

        scheduler.run()

        self.assertEqual(expectedExecutionOrder, completedTasklets)

        completedTasklets = []

        c2 = scheduler.channel()
        c2.preference = 1

        for i in range(3):
            scheduler.tasklet(receiver)(c2, i)

        for i in range(3):
            scheduler.tasklet(sender)(c2, i)

        scheduler.run()

        self.assertEqual(expectedExecutionOrder, completedTasklets)


    def test_preference_receiver(self):
        completedSendTasklets = []

        c = scheduler.channel()

        # this is the default, but setting it explicitly for the test
        c.preference = -1

        def sender(chan, x):
            chan.send(x)
            completedSendTasklets.append(x)

        for i in range(10):
            tasklet = scheduler.tasklet(sender)(c, i)
            tasklet.run()

        for i in range(10):
            c.receive()
            self.assertEqual(0, len(completedSendTasklets))

        scheduler.run()

        self.assertEqual(10, len(completedSendTasklets))

    def test_preference_neither_simple(self):
        taskletComplete = [False]

        c = scheduler.channel()

        c.preference = 0

        def receiving_callable():
            c.receive()
            taskletComplete[0] = True

        scheduler.tasklet(receiving_callable)()

        scheduler.run()

        self.assertFalse(taskletComplete[0])

        c.send(None)

        self.assertFalse(taskletComplete[0])

        scheduler.run()

        self.assertTrue(taskletComplete[0])


    def test_preference_neither(self):
        completedTasklets = []

        c = scheduler.channel()

        c.preference = 0

        def justAnotherTasklet(x):
            completedTasklets.append(x)

        scheduler.tasklet(justAnotherTasklet)("actually first")
        scheduler.tasklet(justAnotherTasklet)("actually second")

        def sender(chan, x):
            scheduler.tasklet(justAnotherTasklet)("sender inbetween")
            chan.send("test")
            completedTasklets.append(("sender", x))

        def receiver(chan, x):
            scheduler.tasklet(justAnotherTasklet)("recever inbetween")
            res = chan.receive()
            completedTasklets.append(("receiver", x))
            self.assertEqual(res, "test")

        for i in range(10):
            scheduler.tasklet(sender)(c, i)

        for i in range(10):
            scheduler.tasklet(receiver)(c, 1)


        self.assertEqual(len(completedTasklets), 0)

        scheduler.tasklet(justAnotherTasklet)("fist")
        scheduler.tasklet(justAnotherTasklet)("second")
        scheduler.tasklet(justAnotherTasklet)("third")
        scheduler.tasklet(justAnotherTasklet)("fourth")
        scheduler.tasklet(justAnotherTasklet)("fifth")

        scheduler.run()

        self.assertEqual(completedTasklets, ['actually first', 'actually second', ('receiver', 1), ('receiver', 1), ('receiver', 1), ('receiver', 1), ('receiver', 1), ('receiver', 1), ('receiver', 1), ('receiver', 1), ('receiver', 1), ('receiver', 1), 'fist', 'second', 'third', 'fourth', 'fifth', 'sender inbetween', 'sender inbetween', 'sender inbetween', 'sender inbetween', 'sender inbetween', 'sender inbetween', 'sender inbetween', 'sender inbetween', 'sender inbetween', 'sender inbetween', 'recever inbetween', ('sender', 0), 'recever inbetween', ('sender', 1), 'recever inbetween', ('sender', 2), 'recever inbetween', ('sender', 3), 'recever inbetween', ('sender', 4), 'recever inbetween', ('sender', 5), 'recever inbetween', ('sender', 6), 'recever inbetween', ('sender', 7), 'recever inbetween', ('sender', 8), 'recever inbetween', ('sender', 9)])

        self.assertEqual(len(completedTasklets), 47)

    def test_channel_iterator_interface(self):
        channel = scheduler.channel()

        def send_value(x):
            channel.send(x)

        scheduler.tasklet(send_value)(1)
        scheduler.tasklet(send_value)(2)
        scheduler.tasklet(send_value)(3)

        scheduler.run()

        iterator = iter(channel)

        self.assertEqual(next(iterator),1)
        self.assertEqual(next(iterator),2)
        self.assertEqual(next(iterator),3)

        scheduler.run()

        self.assertEqual(self.getruncount(),1)

        scheduler.tasklet(send_value)(1)
        scheduler.tasklet(send_value)(2)
        scheduler.tasklet(send_value)(3)

        scheduler.run()

        iterator = iter(channel)

        count = 0

        channel.close()

        for sent_value in iterator:
            count = count + sent_value

        self.assertEqual(count,6)

    def test_send_on_closed(self):
        c = scheduler.channel()
        c.close()
        self.assertRaises(ValueError, c.send, None)

    def test_receive_on_closed(self):
        c = scheduler.channel()
        c.close()
        self.assertRaises(ValueError, c.receive)

    def test_closing(self):
        c = scheduler.channel()
        testSendValue = 101

        def foo():
            c.send(testSendValue)

        scheduler.tasklet(foo)()
        scheduler.run()

        self.assertEqual(c.balance,1)

        c.close()

        self.assertFalse(c.closed)
        self.assertTrue(c.closing)

        self.assertEqual(c.receive(), testSendValue)

        self.assertTrue(c.closed)
        self.assertTrue(c.closing)

        self.assertRaises(ValueError, c.receive)

    def test_open(self):
        testValue = 101
        c = scheduler.channel()
        c.close()

        self.assertTrue(c.closed)

        c.open()

        self.assertFalse(c.closing)
        self.assertFalse(c.closed)

        def foo():
            c.send(testValue)

        scheduler.tasklet(foo)()

        scheduler.run()

        self.assertEqual(c.receive(), testValue)


    def test_iterator_on_closed(self):
        c = scheduler.channel()
        c.close()
        i = iter(c)

        def n():
            return next(i)
        self.assertRaises(StopIteration, n)


    def test_kill_blocked_on_send_on_closed(self):
        c = scheduler.channel()

        def send_tasklet():
            c.send(None)

        t1 = scheduler.tasklet(send_tasklet)()
        t2 = scheduler.tasklet(send_tasklet)()

        scheduler.run()

        c.close()

        self.assertFalse(c.closed)

        t1.kill()

        self.assertFalse(c.closed)

        t2.kill()

        self.assertTrue(c.closed)

        self.assertEqual(c.balance,0)

    def test_kill_blocked_on_receive_on_closed(self):
        c = scheduler.channel()

        def receive_tasklet():
            c.receive()

        t1 = scheduler.tasklet(receive_tasklet)()
        t2 = scheduler.tasklet(receive_tasklet)()

        scheduler.run()

        c.close()

        self.assertFalse(c.closed)

        t1.kill()

        self.assertFalse(c.closed)

        t2.kill()

        self.assertTrue(c.closed)

        self.assertEqual(c.balance,0)

    def test_raise_exception_blocked_on_send_on_closed(self):
        c = scheduler.channel()

        def send_tasklet():
            c.send(None)

        t1 = scheduler.tasklet(send_tasklet)()
        t2 = scheduler.tasklet(send_tasklet)()

        scheduler.run()

        c.close()

        self.assertFalse(c.closed)

        t1.raise_exception(scheduler.TaskletExit)

        self.assertFalse(c.closed)

        t2.raise_exception(scheduler.TaskletExit)

        self.assertTrue(c.closed)

        self.assertEqual(c.balance,0)

    def test_raise_exception_blocked_on_receive_on_closed(self):
        c = scheduler.channel()

        def receive_tasklet():
            c.receive()

        t1 = scheduler.tasklet(receive_tasklet)()
        t2 = scheduler.tasklet(receive_tasklet)()

        scheduler.run()

        c.close()

        self.assertFalse(c.closed)

        t1.raise_exception(scheduler.TaskletExit)

        self.assertFalse(c.closed)

        t2.raise_exception(scheduler.TaskletExit)

        self.assertTrue(c.closed)

        self.assertEqual(c.balance,0)


    def test_invalid_channel_when_skipping_init(self):
        
        class Foo(scheduler.channel):
            def __init__(self, *args, **kwargs):
                pass 
            
        c = Foo()

        self.assertRaises(RuntimeError, c.send)

    def test_invalid_channel_when_skipping_new(self):
        
        class Foo(scheduler.channel):
            def __new__(cls, *args, **kwargs):
                pass 
            
        c = Foo()

        self.assertEqual(c, None)

    def test_pending_kill_blocked_receive_tasklet(self):
        def receiver(chan):
            chan.receive()

        channel = scheduler.channel()
        t = scheduler.tasklet(receiver)(channel)
        t.run()

        self.assertEqual(channel.balance, -1)
        t.kill(pending=True)
        self.assertEqual(channel.balance, 0)

        self.assertTrue(t.alive)
        scheduler.run()
        self.assertFalse(t.alive)


    def test_pending_kill_blocked_send_tasklet(self):
        def sender(chan):
            chan.send(1)
        
        channel = scheduler.channel()
        t = scheduler.tasklet(sender)(channel)
        t.run()

        self.assertEqual(channel.balance, 1)
        t.kill(pending=True)
        self.assertEqual(channel.balance, 0)

        self.assertTrue(t.alive)
        scheduler.run()
        self.assertFalse(t.alive)

    def test_attempting_send_on_block_trapped_tasklet_does_not_change_balance(self):
        channel = scheduler.channel()
        old_block_trap = scheduler.getcurrent().block_trap
        scheduler.getcurrent().block_trap = True
        try:
            with self.assertRaises(RuntimeError):
                channel.send(1)
        finally:
            scheduler.getcurrent().block_trap = old_block_trap
        self.assertEqual(channel.balance, 0)

    def test_attempting_receive_on_block_trapped_tasklet_does_not_change_balance(self):
        channel = scheduler.channel()
        old_block_trap = scheduler.getcurrent().block_trap
        scheduler.getcurrent().block_trap = True
        try:
            with self.assertRaises(RuntimeError):
                channel.receive()
        finally:
            scheduler.getcurrent().block_trap = old_block_trap
        self.assertEqual(channel.balance, 0)


    def test_receive_on_channel_that_had_previously_been_blocked_and_continued_after_an_exception_is_raised_on_it(self):
        
        testValues = [101,202]

        channel = scheduler.channel()

        def receiver(c):
            self.assertEqual(c.receive(),testValues[0])

        def blocker(c):
            try:
                c.receive()
            except:
                pass

            self.assertEqual(c.receive(),testValues[1])

        t = scheduler.tasklet(blocker)(channel)

        scheduler.tasklet(receiver)(channel)

        scheduler.run()

        t.raise_exception(RuntimeError)

        self.assertEqual(channel.balance,-2)

        channel.send(testValues[0])

        channel.send(testValues[1])


    def test_send_on_channel_that_had_previously_been_blocked_and_continued_after_an_exception_is_raised_on_it(self):
        
        testValues = [101,202]

        channel = scheduler.channel()

        def sender(c):
            c.send(testValues[0])

        def blocker(c):
            try:
                c.send(None)
            except:
                pass

            c.send(testValues[1])

        t = scheduler.tasklet(blocker)(channel)

        scheduler.tasklet(sender)(channel)

        scheduler.run()

        t.raise_exception(RuntimeError)

        self.assertEqual(channel.balance,2)

        self.assertEqual(channel.receive(),testValues[0])

        self.assertEqual(channel.receive(),testValues[1])

    def test_nested_channel_with_parent_death_running_fine_and_cleaning_up_correctly(self):
        # If a tasklets parent is dead and then a tasklet attempts to yield to it everything should be fine
        # At end of test everything should clean away
        # This test is a possible segfault test (not reliable but should never happen) and a test of the teardown being 100%
        def n2(chan):
            chan.receive()
            chan.receive()

        def n1(chan):
            t = scheduler.tasklet(n2)(chan)
            t.run()

        channel = scheduler.channel()

        scheduler.tasklet(n1)(channel)

        scheduler.run()

        # Complete first transfer in n2
        channel.send(None)

        # Complete second transfer in n2
        channel.send(None)


    def test_blocked_tasklet_next_is_none(self):

        def foo(c):
            c.receive()

        channel = scheduler.channel()

        t = scheduler.tasklet(foo)(channel)
        scheduler.tasklet(foo)(channel)
        scheduler.run()
        self.assertEqual(t.next, None)

        channel.send(None)
        channel.send(None)

    def test_yielding_to_blocked_tasklet_yields_to_parent(self):
        channel = scheduler.channel()

        r = []

        def otherBlocker(c):
            r.append(c.receive())

        def blocker(c):
            for i in range(10):
                t = scheduler.tasklet(otherBlocker)(c)
                t.run()

            value = c.receive()
            self.assertEqual(10, value)
            r.append(value)

            
        t = scheduler.tasklet(blocker)(channel)
        t.run()

        for i in range(10):
            channel.send(i)

        channel.send(10)

        self.assertEqual(channel.balance, 0)
        self.assertEqual(r, [0,1,2,3,4,5,6,7,8,9,10])

    def test_kill_tasklet_blocked_on_channel_receive(self):
        def receive(c):
            c.receive()

        channel = scheduler.channel()
        t = scheduler.tasklet(receive)(channel)

        scheduler.run()
        t.kill()
        self.assertEqual(channel.balance, 0)

    def test_kill_tasklet_blocked_on_channel_send(self):
        def send(c):
            c.send(1)

        channel = scheduler.channel()
        t = scheduler.tasklet(send)(channel)

        scheduler.run()
        t.kill()
        self.assertEqual(channel.balance, 0)

    def test_set_channel_callback(self):

        def callback1(channel, tasklet, is_sending, will_block):
            pass

        def callback2(channel, tasklet, is_sending, will_block):
            pass

        self.assertEqual(scheduler.get_channel_callback(),None)
        self.assertEqual(None, scheduler.set_channel_callback(callback1))
        self.assertEqual(scheduler.get_channel_callback(),callback1)
        self.assertEqual(callback1, scheduler.set_channel_callback(callback2))
        self.assertEqual(scheduler.get_channel_callback(),callback2)
        self.assertEqual(callback2, scheduler.set_channel_callback(None))
        self.assertEqual(scheduler.get_channel_callback(),None)

    def test_channel_callback_with_blocking_send(self):
        callbackOutput = []

        def channel_callback(channel, tasklet, is_sending, will_block):
            callbackOutput.append([channel,tasklet,is_sending,will_block])

        scheduler.set_channel_callback(channel_callback)
    
        c = scheduler.channel()
        testValue = "VALUE"

        def sending_tasklet(val):
            c.send(val)

        def receiving_tasklet(expected):
            value = c.receive()
            self.assertEqual(value,expected)

        # Test Blocking send    
        t1 = scheduler.tasklet(sending_tasklet)(testValue)
        t2 = scheduler.tasklet(receiving_tasklet)(testValue)

        scheduler.run()

        self.assertEqual(callbackOutput[0][0],c)
        self.assertEqual(callbackOutput[0][1],t1)
        self.assertEqual(callbackOutput[0][2],True)
        self.assertEqual(callbackOutput[0][3],True)

        self.assertEqual(callbackOutput[1][0],c)
        self.assertEqual(callbackOutput[1][1],t2)
        self.assertEqual(callbackOutput[1][2],False)
        self.assertEqual(callbackOutput[1][3],False)

        self.assertEqual(c.balance, 0)

        # Channel callback is global, clean up here to not leak into other tests when running all as one
        scheduler.set_channel_callback(None)
        

    def test_channel_callback_with_blocking_receive(self):
        callbackOutput = []

        def channel_callback(channel, tasklet, is_sending, will_block):
            callbackOutput.append([channel,tasklet,is_sending,will_block])

        scheduler.set_channel_callback(channel_callback)
    
        c = scheduler.channel()
        testValue = "VALUE"

        def sending_tasklet(val):
            c.send(val)

        def receiving_tasklet(expected):
            value = c.receive()
            self.assertEqual(value,expected)

        # Test Blocking send    
        t1 = scheduler.tasklet(receiving_tasklet)(testValue)
        t2 = scheduler.tasklet(sending_tasklet)(testValue)

        scheduler.run()

        self.assertEqual(callbackOutput[0][0],c)
        self.assertEqual(callbackOutput[0][1],t1)
        self.assertEqual(callbackOutput[0][2],False)
        self.assertEqual(callbackOutput[0][3],True)

        self.assertEqual(callbackOutput[1][0],c)
        self.assertEqual(callbackOutput[1][1],t2)
        self.assertEqual(callbackOutput[1][2],True)
        self.assertEqual(callbackOutput[1][3],False)

        self.assertEqual(c.balance, 0)

        # Channel callback is global, clean up here to not leak into other tests when running all as one
        scheduler.set_channel_callback(None)

    def test_channel_test_clear_blocked(self):
        exitOutput = []

        TASKLET1_ID = "TASKLET1"
        TASKLET2_ID = "TASKLET2"

        channel = scheduler.channel()

        # Function to block when run in a tasklet.
        def f(taskletId):
            try:
                channel.send(taskletId)
            except scheduler.TaskletExit:
                exitOutput.append(taskletId)

        # Get the tasklet blocked on the channel.
        tasklet1 = scheduler.tasklet(f)(TASKLET1_ID)
        tasklet2 = scheduler.tasklet(f)(TASKLET2_ID)

        self.assertEqual(self.getruncount(), 3)

        scheduler.run()

        self.assertEqual(self.getruncount(), 1)

        # The tasklet should be blocked.
        self.assertTrue(tasklet1.blocked)
        self.assertTrue(tasklet2.blocked)

        # The channel should have a balance indicating one blocked sender.
        self.assertEqual(channel.balance, 2)

        # Tasklets should be blocked
        self.assertEqual(len(exitOutput),0)

        # Clear all blocked tasklets
        channel.clear()

        # All has been unblocked on channel
        self.assertEqual(channel.balance, 0)

        # Tasklets reflect cleanup
        self.assertFalse(tasklet1.blocked)
        self.assertFalse(tasklet1.alive)

        self.assertFalse(tasklet2.blocked)
        self.assertFalse(tasklet2.alive)

        # Ensure TaskletExit was called
        self.assertEqual(len(exitOutput), 2)
        self.assertEqual(exitOutput[0],TASKLET1_ID)
        self.assertEqual(exitOutput[1],TASKLET2_ID)

    def test_pending_kill_on_completed_transfer_prefer_sender(self):

        def receive(c):
            c.receive()
            # Code should never get past this point
            self.fail("Code Should not be reached")
 
        def send(c):
            c.send(None)

        # Get the tasklet blocked on the channel.
        channel = scheduler.channel()
        channel.preference = 1
        receiver = scheduler.tasklet(receive)(channel)
        sender = scheduler.tasklet(send)(channel)

        receiver.run()

        receiver.kill(pending=True)
        receiver = None
        sender = None
        scheduler.run()

    def test_pending_kill_on_completed_transfer_prefer_receiver(self):

        def receive(c):
            c.receive()
            
        def send(c):
            c.send(None)
            # Code should never get past this point
            self.fail("Code Should not be reached")

        # Get the tasklet blocked on the channel.
        channel = scheduler.channel()
        sender = scheduler.tasklet(send)(channel)
        receiver = scheduler.tasklet(receive)(channel)
        
        sender.run()

        sender.kill(pending=True)
        receiver = None
        sender = None
        scheduler.run()

    def test_channel_args_refcount_prefer_receive(self):
        class Data:
            def __init__(self):
                self.i = 0

            def inc(self):
                self.i = self.i + 1

            def get(self):
                return self.i

        limit = 100
        def ping(chan, val):
            while val.get() < limit:
                val.inc()
                chan.send(val)
                val = chan.receive()

        def pong(chan):
            val = chan.receive()
            while val.get() < limit:
                val.inc()
                chan.send(val)
                if val.get() == limit:
                    return
                val = chan.receive()

        c = scheduler.channel()
        c.preference = -1
        v = Data()

        originalRefcount = sys.getrefcount(v)

        scheduler.tasklet(ping)(c, v)
        scheduler.tasklet(pong)(c)
        scheduler.run()

        endingRefcount = sys.getrefcount(v)

        self.assertEqual(originalRefcount, endingRefcount)

    def test_channel_args_refcount_prefer_sender(self):
        class Data:
            def __init__(self):
                self.i = 0

            def inc(self):
                self.i = self.i + 1

            def get(self):
                return self.i

        limit = 100
        def ping(chan, val):
            while val.get() < limit:
                val.inc()
                chan.send(val)
                val = chan.receive()

        def pong(chan):
            val = chan.receive()
            while val.get() < limit:
                val.inc()
                chan.send(val)
                if val.get() == limit:
                    return
                val = chan.receive()

        c = scheduler.channel()
        c.preference = 1
        v = Data()

        originalRefcount = sys.getrefcount(v)

        scheduler.tasklet(ping)(c, v)
        scheduler.tasklet(pong)(c)
        scheduler.run()

        endingRefcount = sys.getrefcount(v)

        self.assertEqual(originalRefcount, endingRefcount)

    def test_channel_receive_queue_order(self):
        chan = scheduler.channel()

        def rcv(c):
            c.receive()

        t1 = scheduler.tasklet(rcv)(chan)
        t2 = scheduler.tasklet(rcv)(chan)

        scheduler.run()

        a = chan.queue
        self.assertEqual(a, t1)
        chan.send(None)
        a = chan.queue
        self.assertEqual(a, t2)
        chan.send(None)

    def test_channel_send_queue_order(self):
        chan = scheduler.channel()

        def send(c):
            c.send(None)

        t1 = scheduler.tasklet(send)(chan)
        t2 = scheduler.tasklet(send)(chan)

        scheduler.run()

        a = chan.queue
        self.assertEqual(a, t1)
        chan.receive()
        a = chan.queue
        self.assertEqual(a, t2)
        chan.receive()

    def test_tasklet_channel_cleanup_on_thread_finish(self):
        ''' Test that tasklets belonging to a finished thread are removed from channel. '''
        import threading
        c = scheduler.channel()

        tasklet = [None]
        testValue = [False]

        def callable():
            c.receive()
            # Code should never be reached
            testValue[0] = True

        def thread_func():
            tasklet[0] = scheduler.tasklet(callable)()
            tasklet[0].run()

        thread = threading.Thread(target=thread_func)
        thread.start()
        thread.join()

        self.assertEqual(testValue[0],False)

        # The Tasklet will have been removed from blocked
        self.assertEqual(c.balance,0)

        # There should now only be one reference remaining (2 for sys.getrefcount)
        self.assertEqual(sys.getrefcount(tasklet[0]),2)
        tasklet[0] = None
