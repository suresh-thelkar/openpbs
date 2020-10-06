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


import os
import string
import time

from tests.functional import *


class TestStrictOrderingAndBackfilling(TestFunctional):

    """
    Test strict ordering when backfilling is truned off
    """
    @timeout(1800)
    @skipOnCpuSet
    def test_t1(self):

        a = {'resources_available.ncpus': 4}
        self.mom.create_vnodes(a, 1, usenatvnode=True)

        rv = self.scheduler.set_sched_config(
            {'round_robin': 'false all', 'by_queue': 'false all',
             'strict_ordering': 'true all',
             'help_starving_jobs': 'false all'})
        self.assertTrue(rv)

        a = {'backfill_depth': 0}
        self.server.manager(MGR_CMD_SET, SERVER, a)

        j1 = Job(TEST_USER)
        a = {'Resource_List.select': '1:ncpus=2',
             'Resource_List.walltime': 9999}
        j1.set_sleep_time(9999)
        j1.set_attributes(a)
        j1 = self.server.submit(j1)

        j2 = Job(TEST_USER)
        a = {'Resource_List.select': '1:ncpus=3',
             'Resource_List.walltime': 9999}
        j2.set_sleep_time(9999)
        j2.set_attributes(a)
        j2 = self.server.submit(j2)

        j3 = Job(TEST_USER)
        a = {'Resource_List.select': '1:ncpus=2',
             'Resource_List.walltime': 9999}
        j3.set_sleep_time(9999)
        j3.set_attributes(a)
        j3 = self.server.submit(j3)
        rv = self.server.expect(
            JOB,
            {'comment': 'Not Running: Job would break strict sorted order'},
            id=j3,
            offset=2,
            max_attempts=2,
            interval=2)
        self.assertTrue(rv)
    """
    Test strict ordering when queue backilling is enabled and server
    backfilling is off
    """

    @skipOnCpuSet
    def test_t2(self):
        rv = self.scheduler.set_sched_config(
            {'by_queue': 'false prime', 'strict_ordering': 'true all'})
        self.assertTrue(rv)
        a = {'backfill_depth': 2}
        self.server.manager(
            MGR_CMD_SET, QUEUE, a, id='workq')
        a = {
            'queue_type': 'execution',
            'started': 't',
            'enabled': 't',
            'backfill_depth': 1}
        self.server.manager(MGR_CMD_CREATE, QUEUE, a, id='wq2')
        a = {
            'queue_type': 'execution',
            'started': 't',
            'enabled': 't',
            'backfill_depth': 0}
        self.server.manager(MGR_CMD_CREATE, QUEUE, a, id='wq3')
        a = {'backfill_depth': 0}
        self.server.manager(MGR_CMD_SET, SERVER, a)
        a = {'resources_available.ncpus': 5}
        self.server.manager(MGR_CMD_SET, NODE, a, self.mom.shortname)
        self.server.manager(MGR_CMD_SET, SERVER, {'scheduling': 'False'})
        a = {'Resource_List.select': '1:ncpus=2', ATTR_queue: 'workq'}
        j = Job(TEST_USER, a)
        j.set_sleep_time(100)
        j1id = self.server.submit(j)
        j2id = self.server.submit(j)
        j3id = self.server.submit(j)
        a = {'Resource_List.select': '1:ncpus=1', ATTR_queue: 'wq2'}
        j = Job(TEST_USER, a)
        j.set_sleep_time(100)
        j4id = self.server.submit(j)
        a = {'Resource_List.select': '1:ncpus=1', ATTR_queue: 'wq3'}
        j = Job(TEST_USER, a)
        j.set_sleep_time(100)
        j5id = self.server.submit(j)
        self.server.manager(MGR_CMD_SET, SERVER, {'scheduling': 'True'})
        self.server.expect(JOB,
                           {'job_state': 'R'},
                           id=j1id,
                           max_attempts=30,
                           interval=2)
        self.server.expect(JOB,
                           {'job_state': 'R'},
                           id=j2id,
                           max_attempts=30,
                           interval=2)
        self.server.expect(JOB,
                           {'job_state': 'R'},
                           id=j4id,
                           max_attempts=30,
                           interval=2)
        self.server.expect(JOB,
                           {'job_state': 'Q'},
                           id=j5id,
                           max_attempts=30,
                           interval=2)

    @skipOnCpuSet
    def test_zero_backfill_depth_on_queue(self):
        """
        Test if scheduler tries to run a job when strict ordering is enabled
        and backfill_depth is set to 0 on the queue
        """
        a = {'resources_available.ncpus': 2}
        self.mom.create_vnodes(a, 1, usenatvnode=True)

        rv = self.scheduler.set_sched_config(
            {'round_robin': 'false all', 'by_queue': 'false all',
             'strict_ordering': 'true all',
             'help_starving_jobs': 'false all'})
        self.assertTrue(rv)

        a = {'backfill_depth': 0}
        self.server.manager(MGR_CMD_SET, QUEUE, a, id="workq")

        a = {'scheduling': 'False'}
        self.server.manager(MGR_CMD_SET, SERVER, a)

        a = {'Resource_List.select': '1:ncpus=1'}
        j1 = Job(TEST_USER)
        j1.set_attributes(a)
        jid1 = self.server.submit(j1)

        a = {'Resource_List.select': '1:ncpus=2'}
        j2 = Job(TEST_USER)
        j2.set_attributes(a)
        jid2 = self.server.submit(j2)

        a = {'Resource_List.select': '1:ncpus=1'}
        j3 = Job(TEST_USER)
        j3.set_attributes(a)
        jid3 = self.server.submit(j3)

        a = {'scheduling': 'True'}
        self.server.manager(MGR_CMD_SET, SERVER, a)

        job_comment = "Not Running: Job would break strict sorted order"
        self.server.expect(JOB, {'comment': job_comment}, id=jid3, offset=2,
                           max_attempts=2, interval=2)

        # Now try the same scenario with backfilling set to one and check
        # that first and third job runs but second gets calendared.
        # since we want thrid job to backfill around second, we need to make
        # sure that walltime of third job is less than the walltime of first
        # job
        a = {'scheduling': 'False'}
        self.server.manager(MGR_CMD_SET, SERVER, a)
        self.server.deljob([jid1, jid2, jid3])

        a = {'backfill_depth': 1}
        self.server.manager(MGR_CMD_SET, QUEUE, a, id="workq")

        a = {'Resource_List.select': '1:ncpus=1',
             'Resource_List.walltime': '100'}
        j4 = Job(TEST_USER)
        j4.set_attributes(a)
        jid4 = self.server.submit(j4)

        a = {'Resource_List.select': '1:ncpus=2'}
        j5 = Job(TEST_USER)
        j5.set_attributes(a)
        jid5 = self.server.submit(j5)

        a = {'Resource_List.select': '1:ncpus=1',
             'Resource_List.walltime': '50'}
        j6 = Job(TEST_USER)
        j6.set_attributes(a)
        jid6 = self.server.submit(j6)

        a = {'scheduling': 'True'}
        self.server.manager(MGR_CMD_SET, SERVER, a)

        self.server.expect(JOB, {'job_state': 'R'}, id=jid4)
        self.server.expect(JOB, {'job_state': 'R'}, id=jid6)
        self.scheduler.log_match(jid5 + ";Job is a top job")

    @skipOnCpuSet
    def test_zero_backfill_depth_on_one_queue(self):
        """
        Test if scheduler tries to run a job when strict ordering is enabled
        and backfill_depth is set to 0 on one queue but backfill_depth is
        enabled on another queue.
        """
        a = {'resources_available.ncpus': 1}
        self.mom.create_vnodes(a, 1, usenatvnode=True)

        rv = self.scheduler.set_sched_config(
            {'round_robin': 'false all', 'by_queue': 'false all',
             'strict_ordering': 'true all',
             'help_starving_jobs': 'false all'})
        self.assertTrue(rv)

        a = {'backfill_depth': 0}
        self.server.manager(MGR_CMD_SET, QUEUE, a, id="workq")

        a = {'queue_type': 'execution', 'started': 'True', 'enabled': 'True',
             'priority': '100'}
        self.server.manager(MGR_CMD_CREATE, QUEUE, a, id="workq2")

        a = {'backfill_depth': 1}
        self.server.manager(MGR_CMD_SET, QUEUE, a, id="workq2")

        a = {'scheduling': 'False'}
        self.server.manager(MGR_CMD_SET, SERVER, a)

        a = {'Resource_List.select': '1:ncpus=1'}
        j1 = Job(TEST_USER)
        j1.set_attributes(a)
        self.server.submit(j1)

        j2 = Job(TEST_USER)
        j2.set_attributes(a)
        self.server.submit(j2)

        j3 = Job(TEST_USER)
        j3.set_attributes(a)
        jid3 = self.server.submit(j3)

        a = {'Resource_List.select': '1:ncpus=1', 'queue': 'workq2'}
        j4 = Job(TEST_USER)
        j4.set_attributes(a)
        jid4 = self.server.submit(j4)

        j5 = Job(TEST_USER)
        j5.set_attributes(a)
        self.server.submit(j5)

        j6 = Job(TEST_USER)
        j6.set_attributes(a)
        self.server.submit(j6)

        a = {'scheduling': 'True'}
        self.server.manager(MGR_CMD_SET, SERVER, a)
        job_comment = "Not Running: Job would break strict sorted order"
        self.server.expect(JOB, {'comment': job_comment}, id=jid3, offset=2,
                           max_attempts=2, interval=2)
        self.scheduler.log_match(jid4 + ";Job is a top job")
