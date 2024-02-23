from __future__ import absolute_import
import sys
import random
import unittest
import scheduler

from support import SchedulerTestCase, require_one_thread, get_current_watchdog_list


# Helpers

def is_soft():
    softswitch = scheduler.enable_softswitch(0)
    scheduler.enable_softswitch(softswitch)
    return softswitch


class SimpleScheduler(object):
    """ Not really scheduler as such but used here to implement
    autoscheduling hack and store a schedule count. """

    def __init__(self, bytecodes=25, softSchedule=False):
        self.bytecodes = bytecodes
        self.schedule_count = 0
        self.softSchedule = softSchedule

    def get_schedule_count(self):
        return self.schedule_count

    def schedule_cb(self, task):
        self.schedule_count += 1
        if task:
            task.insert()

    def autoschedule(self):
        while scheduler.runcount > 1:
            try:
                returned = scheduler.run(self.bytecodes, soft=self.softSchedule)

            except Exception as e:

                # Can't clear off exception easily...
                while scheduler.runcount > 1:
                    scheduler.current.next.kill()

                raise e

            else:
                self.schedule_cb(returned)


def runtask6(name):
    me = scheduler.getcurrent()
    cur_depth = me.recursion_depth

    for ii in range(1000):
        assert cur_depth == me.recursion_depth


def runtask_print(name):
    x = 0
    for ii in range(1000):
        x += 1

    return name


def runtask(name):
    x = 0
    for ii in range(1000):
        if ii % 50 == 0:
            sys._getframe()  # a dummy

        x += 1

    return name


def runtask2(name):
    x = 0
    for ii in range(1000):
        if ii % 50 == 0:
            scheduler.schedule()  # same time, but should give up timeslice

        x += 1

    return name


def runtask3(name):
    exec("""
for ii in range(1000):
    pass
""")


def runtask4(name, channel):
    for ii in range(1000):
        if ii % 50 == 0:
            channel.send(name)


def recurse_level_then_do_schedule(count):
    if count == 0:
        scheduler.schedule()
    else:
        recurse_level_then_do_schedule(count - 1)


def runtask5(name):
    for ii in [1, 10, 100, 500]:
        recurse_level_then_do_schedule(ii)


def runtask_atomic_helper(count):
    hold = scheduler.current.set_atomic(1)
    for ii in range(count):
        pass
    scheduler.current.set_atomic(hold)


def runtask_atomic(name):
    for ii in range(10):
        for ii in [1, 10, 100, 500]:
            runtask_atomic_helper(ii)


def runtask_bad(name):
    raise UserWarning


class ServerTasklet(scheduler.tasklet):

    def __init__(self, func, name=None):
        super(ServerTasklet, self).__init__(func)
        if not name:
            name = "at %08x" % (id(self))
        self.name = name

    def __repr__(self):
        return "Tasklet %s" % self.name


def servertask(name, chan):
    self = scheduler.getcurrent()
    self.count = 0
    while True:
        r = chan.receive()
        self.count += 1


class TestWatchdog(SchedulerTestCase):
    softSchedule = False

    def setUp(self):
        super(TestWatchdog, self).setUp()
        self.verbose = __name__ == "__main__"

    def tearDown(self):
        del self.verbose

    def run_tasklets(self, fn, n=100):
        scheduler = SimpleScheduler(n, self.softSchedule)
        tasklets = []
        for name in ["t1", "t2", "t3"]:
            tasklets.append(scheduler.tasklet(fn)(name))
        # allow scheduling with hard switching
        list(map(lambda t: t.set_ignore_nesting(1), tasklets))

        scheduler.autoschedule()
        for ii in tasklets:
            self.assertFalse(ii.alive)

        return scheduler.get_schedule_count()

    def test_simple(self):
        self.run_tasklets(runtask)

    def xtest_recursion_count(self):
        self.run_tasklets(runtask6)

    def test_nested(self):
        self.run_tasklets(runtask5)

    def test_nested2(self):
        self.run_tasklets(runtask5, 0)

    def test_tasklet_with_schedule(self):
        # make sure that we get enough tick counting
        hold = sys.getswitchinterval()
        self.addCleanup(sys.setswitchinterval, hold)
        sys.setswitchinterval(0.001)

        n1 = self.run_tasklets(runtask)
        n2 = self.run_tasklets(runtask2)

        sys.setswitchinterval(hold)
        if self.verbose:
            print()
            print(20 * "*", "runtask:", n1, "runtask2:", n2)
        if not self.softSchedule:
            self.assertGreater(n1, n2)
        else:
            self.assertLess(n1, n2)

    def test_exec_tasklet(self):
        self.run_tasklets(runtask3)

    def test_send_recv(self):
        chan = scheduler.channel()
        server = ServerTasklet(servertask)
        server_task = server("server", chan)

        scheduler = SimpleScheduler(100, self.softSchedule)

        tasklets = [scheduler.tasklet(runtask4)(name, chan)
                    for name in ["client1", "client2", "client3"]]

        scheduler.autoschedule()
        self.assertEqual(server.count, 60)

        # Kill server
        self.assertRaises(StopIteration, lambda: chan.send_exception(StopIteration))

    def test_atomic(self):
        self.run_tasklets(runtask_atomic)

    def test_exception(self):
        self.assertRaises(UserWarning, lambda: self.run_tasklets(runtask_bad))

    def get_pickled_tasklet(self):
        orig = scheduler.tasklet(runtask_print)("pickleme")
        orig.set_ignore_nesting(1)
        not_finished = scheduler.run(100)
        self.assertEqual(not_finished, orig)
        return self.dumps(not_finished)

    @SchedulerTestCase.prepare_pickle_test_method
    def test_pickle(self):
        # Run global
        t = self.loads(self.get_pickled_tasklet())
        t.insert()
        if is_soft():
            scheduler.run()
        else:
            self.assertRaises(RuntimeError, scheduler.run)

        # Run on tasklet
        t = self.loads(self.get_pickled_tasklet())
        t.insert()
        if is_soft():
            t.run()
        else:
            self.assertRaises(RuntimeError, t.run)
            return  # enough crap

        # Run on watchdog
        t = self.loads(self.get_pickled_tasklet())
        t.insert()
        while scheduler.runcount > 1:
            returned = scheduler.run(100)

    @SchedulerTestCase.prepare_pickle_test_method
    def test_run_return(self):
        # if the main tasklet had previously gone into C stack recusion-based switch, stackless.run() would give
        # strange results
        # this would happen after, e.g. tasklet pickling and unpickling
        # note, the bug was hard to repro, most of the time, it didn't occur.
        t = self.loads(self.get_pickled_tasklet())

        def func():
            pass
        t = scheduler.tasklet(func)
        t()
        r = scheduler.run()
        self.assertEqual(r, None)

    def test_lone_receive(self):

        def f():
            scheduler.channel().receive()
        scheduler.tasklet(f)()
        scheduler.run()


class TestWatchdogSoft(TestWatchdog):
    softSchedule = True

    def __init__(self, *args):
        self.chans = [scheduler.channel() for i in range(3)]
        # for c in self.chans:
        #    c.preference = 0
        TestWatchdog.__init__(self, *args)

    def ChannelTasklet(self, i):
        a = i
        b = (i + 1) % 3
        recv = False  # to bootstrap the cycle
        while True:
            # print a
            if i != 0 or recv:
                d = self.chans[a].receive()
            recv = True
            j = 0
            for i in range(random.randint(100, 1000)):
                j = i + i

            self.chans[b].send(j)

    # test the soft interrupt on a chain of tasklets running
    def test_channelchain(self):
        c = [scheduler.tasklet(self.ChannelTasklet) for i in range(3)]
        # print sys.getcheckinterval()
        for i, t in enumerate(reversed(c)):
            t(i)
        try:
            for i in range(10):
                scheduler.run(50000, soft=True, totaltimeout=True, ignore_nesting=True)
                # print "**", stackless.runcount
                self.assertTrue(scheduler.runcount == 3 or scheduler.runcount == 4)
        finally:
            for t in c:
                t.kill()


class TestDeadlock(SchedulerTestCase):
    """Test various deadlock scenarios"""

    @require_one_thread
    def testReceiveOnMain(self):
        """Thest that we get a deadock exception if main tries to block"""
        self.c = scheduler.channel()
        self.assertRaisesRegex(RuntimeError, "Deadlock", self.c.receive)

    def test_main_receiving_endttasklet(self):
        """Test that the main tasklet is interrupted when a tasklet ends"""
        c = scheduler.channel()
        t = scheduler.tasklet(lambda: None)()
        self.assertRaisesRegex(RuntimeError, "receiving", c.receive)

    def test_main_sending_enddtasklet(self):
        """Test that the main tasklet is interrupted when a tasklet ends"""
        c = scheduler.channel()
        t = scheduler.tasklet(lambda: None)()
        self.assertRaisesRegex(RuntimeError, "sending", c.send, None)

    def test_main_gets_exception(self):
        """Test that a custom exception is transfered to a blocked main"""
        def task():
            raise ZeroDivisionError("mumbai")
        scheduler.tasklet(task)()
        self.assertRaisesRegex(ZeroDivisionError, "mumbai", scheduler.channel().receive)

    @require_one_thread
    def test_tasklet_deadlock(self):
        """Test that a tasklet gets the "Deadlock" exception"""
        mc = scheduler.channel()

        def task():
            c = scheduler.channel()
            self.assertRaisesRegex(RuntimeError, "Deadlock", c.receive)
            mc.send(None)
        t = scheduler.tasklet(task)()
        mc.receive()

    @require_one_thread
    def test_tasklet_and_main_receive(self):
        """Test that the tasklet's deadlock exception gets transferred to a blocked main"""
        mc = scheduler.channel()

        def task():
            scheduler.channel().receive()
        t = scheduler.tasklet(task)()
        # main should get the tasklet's exception
        self.assertRaisesRegex(RuntimeError, "Deadlock", mc.receive)

    def test_error_propagation_when_not_deadlock(self):
        def task1():
            scheduler.schedule()

        def task2():
            raise ZeroDivisionError("bar")

        t1 = scheduler.tasklet(task1)()
        t2 = scheduler.tasklet(task2)()
        self.assertRaisesRegex(ZeroDivisionError, "bar", scheduler.run)


class TestNewWatchdog(SchedulerTestCase):
    """Tests for running stackless.run on non-main tasklet, and having nested run invocations"""

    def worker_func(self):
        scheduler.schedule()
        self.done += 1

    def setUp(self):
        super(TestNewWatchdog, self).setUp()
        self.done = 0
        self.worker = scheduler.tasklet(self.worker_func)()
        self.watchdog_list = get_current_watchdog_list()
        self.assertListEqual(self.watchdog_list, [None], "Watchdog list is not empty before test: %r" % (self.watchdog_list,))

    def tearDown(self):
        try:
            self.assertListEqual(self.watchdog_list, [None], "Watchdog list is not empty after test: %r" % (self.watchdog_list,))
        except AssertionError:
            self.watchdog_list[0] = None
            self.watchdog_list[1:] = []
            raise
        super(TestNewWatchdog, self).tearDown()

    def test_run_from_worker(self):
        """Test that run() works from a different tasklet"""
        def runner_func():
            scheduler.run()
            self.done += 1
        t = scheduler.tasklet(runner_func)()

        # main runs as a normal tasklet now
        while not self.done:
            scheduler.schedule()
        # the runner is still paused, because the main tasklet wasn't blocked
        self.assertEqual(self.done, 1)
        # make runner exit
        t.run()
        self.assertTrue(self.done, 2)

    def test_run_from_worker_main_blocked(self):
        """main is blocked while a tasklet calls stackless.run()"""
        c = scheduler.channel()

        def runner_func():
            scheduler.run()
            self.done += 1
            c.send(None)
        t = scheduler.tasklet(runner_func)()

        # main blocks
        c.receive()
        self.assertEqual(self.done, 2)

    def test_run_from_worker_main_running(self):
        """Main calls run() to start inner tasklet that also calls run()"""
        def runner_func():
            scheduler.run()
            self.assertEqual(self.done, 1)
            self.done += 1
        t = scheduler.tasklet(runner_func)()

        # main calls run
        scheduler.run()
        self.assertEqual(self.done, 2)

    def test_inner_run_completes_first(self):
        """Test that the outer run() is indeed paused when the inner one completes"""
        def runner_func():
            scheduler.run()
            self.assertTrue(scheduler.main.paused)
            self.done += 1
        scheduler.tasklet(runner_func)()
        scheduler.run()
        self.assertEqual(self.done, 2)

    def test_inner_run_gets_error(self):
        """Test that an unhandled error is passed to the inner watchdog"""
        def errfunc():
            raise RuntimeError("foo")

        def runner_func():
            scheduler.tasklet(errfunc)()
            self.assertRaisesRegex(RuntimeError, "foo", scheduler.run)
            self.done += 1
        scheduler.tasklet(runner_func)()
        scheduler.run()
        self.assertEqual(self.done, 2)

    def test_manual_wakeup(self):
        """with nested run, the main tasklet is manually woken up, implicitly waking up the inner watchdogs."""
        def wakeupfunc():
            scheduler.main.run()
            self.done += 1

        def runner_func():
            scheduler.tasklet(wakeupfunc)()
            scheduler.run()
            self.done += 1
        scheduler.tasklet(runner_func)()
        scheduler.run()
        self.assertEqual(self.done, 1)  # only worker func has run now
        # empty all tasklets
        scheduler.run()
        self.assertEqual(self.done, 3)  # all tasklets have completed.

    def test_main_exiting(self):
        """Verify behavior when main continues running and a taskler runs a watchdog """
        def runner_func():
            scheduler.run()
            self.done += 1

        t = scheduler.tasklet(runner_func)()

        # let the scheduler run
        while not self.done:
            scheduler.schedule()
        self.assertEqual(self.done, 1)  # only worker has finished.

        # now, run stackless.run here
        scheduler.run()
        # but nothing happened, because the other watchdog is not runnable
        self.assertEqual(self.done, 1)  # only worker has finished.
        scheduler.run()
        self.assertEqual(self.done, 1)  # the other tasklet is blocked.
        scheduler.schedule()
        self.assertEqual(self.done, 1)  # The other dude won't exit its run until we are no longer runnable.
        self.assertTrue(t.alive)
        t.kill()
        self.assertFalse(t.alive)

    def _test_watchdog_on_tasklet(self, soft):
        def runner_func():
            # run the watchdog long enough to start t1 and t2
            scheduler.run(150, soft=soft, totaltimeout=True, ignore_nesting=True)
            if scheduler.getruncount():
                self.done += 1  # we were interrupted
            t1.kill()
            t2.kill()

        def task():
            while True:
                for i in range(500):
                    i = i
                scheduler.schedule()

        t1 = scheduler.tasklet(task)()
        t2 = scheduler.tasklet(task)()
        t3 = scheduler.tasklet(runner_func)()

        scheduler.run()
        self.assertEqual(self.done, 2)

    def test_watchdog_on_tasklet_soft(self):
        """Verify that the tasklet running the watchdog is the one awoken"""
        self._test_watchdog_on_tasklet(True)

    def test_watchdog_on_tasklet_hard(self):
        """Verify that the tasklet running the watchdog is the one awoken (hard)"""
        self._test_watchdog_on_tasklet(False)

    def _test_watchdog_priority(self, soft):
        self.awoken = 0

        def runner_func(recursive, start):
            if recursive:
                scheduler.tasklet(runner_func)(recursive - 1, start)
            with scheduler.atomic():
                scheduler.run(10000, soft=soft, totaltimeout=True, ignore_nesting=True)
                a = self.awoken
                self.awoken += 1
            if recursive == start:
                # we are the first watchdog
                self.assertEqual(a, 0)  # the first to wake up
                self.done += 1  # we were interrupted
            t1.kill()
            t2.kill()

        def task():
            while True:
                for i in range(100):
                    i = i
                scheduler.schedule()

        t1 = scheduler.tasklet(task)()
        t2 = scheduler.tasklet(task)()
        t3 = scheduler.tasklet(runner_func)(3, 3)
        scheduler.run()
        self.assertEqual(self.done, 2)

    def test_watchdog_priority_soft(self):
        """Verify that outermost "real" watchdog gets awoken"""
        self._test_watchdog_priority(True)

    def test_watchdog_priority_hard(self):
        """Verify that outermost "real" watchdog gets awoken (hard)"""
        self._test_watchdog_priority(False)

    def _test_schedule_deeper(self, soft):
        # get rid of self.worker
        scheduler.run()
        self.assertFalse(self.worker.alive)
        self.assertEqual(self.done, 1)
        self.assertListEqual([None], self.watchdog_list)

        tasklets = [None, None, None]  # watchdog1, watchdog2, worker

        def worker():
            self.assertListEqual([tasklets[0], scheduler.main, tasklets[0], tasklets[1]], self.watchdog_list)
            self.assertFalse(tasklets[1].scheduled)
            tasklets[1].insert()
            self.done += 1
            for i in range(100):
                for j in range(100):
                    dummy = i * j
            if soft:
                scheduler.schedule()
            self.done += 1

        def watchdog2():
            tasklets[2] = scheduler.tasklet(worker)()
            scheduler.run()

        def watchdog1():
            tasklets[1] = scheduler.tasklet(watchdog2)()
            victim = scheduler.run(1000, soft=soft, ignore_nesting=True, totaltimeout=True)
            self.assertEqual(self.done, 2, "worker interrupted too early or to late, adapt timeout: %d" % self.done)
            if not soft:
                self.assertEqual(tasklets[2], victim)

        tasklets[0] = scheduler.tasklet(watchdog1)()
        scheduler.run()
        self.assertLessEqual([None], self.watchdog_list)
        scheduler.run()
        # avoid a resource leak
        for t in tasklets:
            t.kill()

    def test_schedule_deeper_soft(self):
        self._test_schedule_deeper(True)

    def test_schedule_deeper_hard(self):
        self._test_schedule_deeper(False)


if __name__ == '__main__':
    if not sys.argv[1:]:
        sys.argv.append('-v')

    scheduler.enable_softswitch(True)
    unittest.main()
