# coding: utf-8

# Copyright (C) 1994-2020 Altair Engineering, Inc.
# For more information, contact Altair at www.altair.com.
#
# This file is part of both the OpenPBS software ("OpenPBS")
# and the PBS Professional ("PBS Pro") software.
#
# Open Source License Information:
#
# OpenPBS is free software. You can redistribute it and/or modify it under
# the terms of the GNU Affero General Public License as published by the
# Free Software Foundation, either version 3 of the License, or (at your
# option) any later version.
#
# OpenPBS is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Affero General Public
# License for more details.
#
# You should have received a copy of the GNU Affero General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#
# Commercial License Information:
#
# PBS Pro is commercially licensed software that shares a common core with
# the OpenPBS software.  For a copy of the commercial license terms and
# conditions, go to: (http://www.pbspro.com/agreement.html) or contact the
# Altair Legal Department.
#
# Altair's dual-license business model allows companies, individuals, and
# organizations to create proprietary derivative works of OpenPBS and
# distribute them - whether embedded or bundled with other software -
# under a commercial license agreement.
#
# Use of Altair's trademarks, including but not limited to "PBS™",
# "OpenPBS®", "PBS Professional®", and "PBS Pro™" and Altair's logos is
# subject to Altair's trademark licensing policies.


from tests.resilience import *
from time import sleep


class TestPbsHookAlarmLargeMultinodeJob(TestResilience):

    """
    This test suite contains hooks test to verify that a large
    multi-node job does not slow down hook execution and cause an alarm.
    """

    def setUp(self):

        TestResilience.setUp(self)
        # Increasing the daemon log for debugging
        self.server.manager(MGR_CMD_SET, SERVER, {"log_events": '2047'})
        self.mom.add_config({"$logevent": "0xfffffff"})

        if not self.mom.is_cpuset_mom():
            a = {'resources_available.mem': '1gb',
                 'resources_available.ncpus': '1'}
            self.mom.create_vnodes(a, 5000, expect=False)
            # Make sure all the nodes are in state free.  We can't let
            # create_vnodes() do this because it does
            # a pbsnodes -v on each vnode.
            # This takes a long time.
            self.server.expect(NODE, {'state=free': (GE, 5000)})
            # Restart mom explicitly due to PP-993
            self.mom.restart()

    def submit_job(self):
        a = {'Resource_List.walltime': 10}
        if self.mom.is_cpuset_mom():
            vnode_val = self.server.status(NODE)
            del vnode_val[0]
            vnode_id = vnode_val[0]['id']
            ncpus = vnode_val[0]['resources_available.ncpus']
            del vnode_val[0]
            a = {'Resource_List.select': '1:ncpus=' +
                 ncpus + ':vnode=' + vnode_id}
            for _vnode in vnode_val:
                vnode_id = _vnode['id']
                ncpus = _vnode['resources_available.ncpus']
                a['Resource_List.select'] += '+1:ncpus=' + \
                    ncpus + ':vnode=' + vnode_id
        else:
            a['Resource_List.select'] = '5000:ncpus=1:mem=1gb'
        j = Job(TEST_USER)

        j.set_attributes(a)
        j.set_sleep_time(10)

        jid = self.server.submit(j)
        return jid

    def test_begin_hook(self):
        """
        Create an execjob_begin hook, import a hook content with a small
        alarm value, and test it against a large multi-node job.
        """
        hook_name = "beginhook"
        hook_event = "execjob_begin"
        hook_body = """
import pbs
e=pbs.event()
pbs.logmsg(pbs.LOG_DEBUG, "executing begin hook %s" % (e.hook_name,))
"""
        a = {'event': hook_event, 'enabled': 'True',
             'alarm': '60'}
        self.server.create_import_hook(hook_name, a, hook_body)

        jid = self.submit_job()

        self.server.expect(JOB, {'job_state': 'R'},
                           jid, max_attempts=100, interval=2)
        self.mom.log_match(
            "pbs_python;executing begin hook %s" % (hook_name,), n=100,
            max_attempts=5, interval=5, regexp=True)

        self.mom.log_match(
            "Job;%s;alarm call while running %s hook" % (jid, hook_event),
            n=100, max_attempts=5, interval=5, regexp=True, existence=False)

        self.mom.log_match("Job;%s;Started, pid" % (jid,), n=100,
                           max_attempts=5, interval=5, regexp=True)

    def test_prolo_hook(self):
        """
        Create an execjob_prologue hook, import a hook content with a
        small alarm value, and test it against a large multi-node job.
        """
        hook_name = "prolohook"
        hook_event = "execjob_prologue"
        hook_body = """
import pbs
e=pbs.event()
pbs.logmsg(pbs.LOG_DEBUG, "executing prologue hook %s" % (e.hook_name,))
"""
        a = {'event': hook_event, 'enabled': 'True',
             'alarm': '60'}
        self.server.create_import_hook(hook_name, a, hook_body)

        jid = self.submit_job()

        self.server.expect(JOB, {'job_state': 'R'},
                           jid, max_attempts=100, interval=2)

        self.mom.log_match(
            "pbs_python;executing prologue hook %s" % (hook_name,), n=100,
            max_attempts=5, interval=5, regexp=True)

        self.mom.log_match(
            "Job;%s;alarm call while running %s hook" % (jid, hook_event),
            n=100, max_attempts=5, interval=5, regexp=True, existence=False)

    def test_epi_hook(self):
        """
        Create an execjob_epilogue hook, import a hook content with a small
        alarm value, and test it against a large multi-node job.
        """
        hook_name = "epihook"
        hook_event = "execjob_epilogue"
        hook_body = """
import pbs
e=pbs.event()
pbs.logmsg(pbs.LOG_DEBUG, "executing epilogue hook %s" % (e.hook_name,))
"""
        search_after = time.time()
        a = {'event': hook_event, 'enabled': 'True',
             'alarm': '60'}
        self.server.create_import_hook(hook_name, a, hook_body)

        jid = self.submit_job()

        self.server.expect(JOB, {'job_state': 'R'},
                           jid, max_attempts=100, interval=2)

        self.logger.info("Wait 10s for job to finish")
        sleep(10)

        self.server.log_match("dequeuing from", max_attempts=100,
                              interval=3, starttime=search_after)

        self.mom.log_match(
            "pbs_python;executing epilogue hook %s" % (hook_name,), n=100,
            max_attempts=5, interval=5, regexp=True)

        self.mom.log_match(
            "Job;%s;alarm call while running %s hook" % (jid, hook_event),
            n=100, max_attempts=5, interval=5, regexp=True, existence=False)

        self.mom.log_match("Job;%s;Obit sent" % (jid,), n=100,
                           max_attempts=5, interval=5, regexp=True)

    def test_end_hook(self):
        """
        Create an execjob_end hook, import a hook content with a small
        alarm value, and test it against a large multi-node job.
        """
        hook_name = "endhook"
        hook_event = "execjob_end"
        hook_body = """
import pbs
e=pbs.event()
pbs.logmsg(pbs.LOG_DEBUG, "executing end hook %s" % (e.hook_name,))
"""
        search_after = time.time()
        a = {'event': hook_event, 'enabled': 'True',
             'alarm': '40'}
        self.server.create_import_hook(hook_name, a, hook_body)

        jid = self.submit_job()
        self.server.expect(JOB, {'job_state': 'R'},
                           jid, max_attempts=100, interval=2)

        self.logger.info("Wait 10s for job to finish")
        sleep(10)

        self.server.log_match("dequeuing from", max_attempts=100,
                              interval=3, starttime=search_after)

        self.mom.log_match(
            "pbs_python;executing end hook %s" % (hook_name,), n=100,
            max_attempts=5, interval=5, regexp=True)

        self.mom.log_match(
            "Job;%s;alarm call while running %s hook" % (jid, hook_event),
            n=100, max_attempts=5, interval=5, regexp=True, existence=False)

        self.mom.log_match("Job;%s;Obit sent" % (jid,), n=100,
                           max_attempts=5, interval=5, regexp=True)
