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


from tests.functional import *


class TestHostResources(TestFunctional):

    @skipOnCpuSet
    def test_set_direct_on_indirect_resc(self):
        """
        Set a direct resource on indirect resource and make sure
        this change is reflected on resource assigned.
        """
        # Create a consumable custom resources 'fooi'
        self.server.add_resource('fooi', 'long', 'nh')
        # Create 2 vnodes with individual hosts
        attr = {'resources_available.ncpus': 1}
        self.mom.create_vnodes(attr, 2, sharednode=False)
        vnode0 = self.mom.shortname + '[0]'
        vnode1 = self.mom.shortname + '[1]'
        self.server.manager(MGR_CMD_SET, NODE,
                            {'resources_available.fooi': 100}, vnode0)
        self.server.manager(MGR_CMD_SET, NODE, {'resources_available.fooi':
                                                '@' + vnode0}, vnode1)
        self.server.manager(MGR_CMD_SET, NODE,
                            {'resources_available.fooi': 100}, vnode1)
        self.server.expect(NODE, {'resources_assigned.fooi': ''},
                           id=vnode1, op=UNSET, max_attempts=1)

    @skipOnCpuSet
    def test_set_direct_on_indirect_resc_busy(self):
        """
        Set a direct resource on indirect resource
        but a busy node and make sure this is failing.
        """
        # Create 2 vnodes with individual hosts
        attr = {'resources_available.ncpus': 1}
        self.mom.create_vnodes(attr, 2, sharednode=False)
        vnode0 = self.mom.shortname + '[0]'
        vnode1 = self.mom.shortname + '[1]'
        # Create a consumable custom resources 'fooi'
        self.server.add_resource('fooi', 'long', 'nh')
        self.scheduler.add_resource("fooi")
        self.server.manager(MGR_CMD_SET, NODE,
                            {'resources_available.fooi': 100}, vnode0)
        self.server.manager(MGR_CMD_SET, NODE, {'resources_available.fooi':
                                                '@' + vnode0}, vnode1)
        # Submit jobs.
        a = {'Resource_List.fooi': 50}
        J = Job(attrs=a)
        self.server.submit(J)
        j2 = self.server.submit(J)
        self.server.expect(JOB, {'job_state': 'R'}, id=j2)
        self.server.expect(NODE, {'state': 'job-busy'}, id=vnode1)
        try:
            self.server.manager(MGR_CMD_SET, NODE,
                                {'resources_available.fooi': 100},
                                vnode1)
        except PbsManagerError:
            pass
        else:
            self.server.expect(NODE, {'resources_available.fooi': 100},
                               id=vnode1, op=UNSET, max_attempts=1)

    @skipOnCpuSet
    def test_set_direct_on_target_node(self):
        """
        Set a direct resource on target node which should
        fail with error.
        A and B are having direct resources.
        C -> B
        B -> A should fail as it is alreay a target.
        """
        # Create 3 vnodes with individual hosts
        attr = {'resources_available.ncpus': 1}
        self.mom.create_vnodes(attr, 3, sharednode=False)
        # Create a consumable custom resources 'fooi'
        self.server.add_resource('fooi', 'long', 'nh')
        vnode0 = self.mom.shortname + '[0]'
        vnode1 = self.mom.shortname + '[1]'
        vnode2 = self.mom.shortname + '[2]'
        self.server.manager(MGR_CMD_SET, NODE,
                            {'resources_available.fooi': 100}, vnode0)
        self.server.manager(MGR_CMD_SET, NODE,
                            {'resources_available.fooi': 100}, vnode1)
        self.server.manager(MGR_CMD_SET, NODE, {'resources_available.fooi':
                                                '@' + vnode1}, vnode2)
        try:
            self.server.manager(MGR_CMD_SET, NODE,
                                {'resources_available.fooi': '@' + vnode0},
                                vnode1)
        except PbsManagerError:
            pass
        else:
            _msg = "Setting indirect resources on a target object should fail"
            self.assertTrue(False, _msg)

    @skipOnCpuSet
    def test_create_node_without_resc_set(self):
        """
        Create a consumable resource then create a new node.
        The resources_assigned value should not get assigned
        on it without explicitely setting resource on the node.
        """
        # Create a consumable custom resources 'fooi'
        self.server.add_resource('fooi', 'long', 'nh')
        self.server.manager(MGR_CMD_DELETE, NODE, None, "")
        self.server.manager(MGR_CMD_CREATE, NODE, id=self.mom.shortname)
        self.server.expect(NODE, {'resources_assigned.fooi': ''},
                           id=self.mom.shortname, op=UNSET, max_attempts=1)
