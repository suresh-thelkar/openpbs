# coding: utf-8

# Copyright (C) 1994-2017 Altair Engineering, Inc.
# For more information, contact Altair at www.altair.com.
#
# This file is part of the PBS Professional ("PBS Pro") software.
#
# Open Source License Information:
#
# PBS Pro is free software. You can redistribute it and/or modify it under the
# terms of the GNU Affero General Public License as published by the Free
# Software Foundation, either version 3 of the License, or (at your option) any
# later version.
#
# PBS Pro is distributed in the hope that it will be useful, but WITHOUT ANY
# WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
# A PARTICULAR PURPOSE. See the GNU Affero General Public License for more
# details.
#
# You should have received a copy of the GNU Affero General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.
#
# Commercial License Information:
#
# The PBS Pro software is licensed under the terms of the GNU Affero General
# Public License agreement ("AGPL"), except where a separate commercial license
# agreement for PBS Pro version 14 or later has been executed in writing with
# Altair.
#
# Altair’s dual-license business model allows companies, individuals, and
# organizations to create proprietary derivative works of PBS Pro and
# distribute them - whether embedded or bundled with other software - under
# a commercial license agreement.
#
# Use of Altair’s trademarks, including but not limited to "PBS™",
# "PBS Professional®", and "PBS Pro™" and Altair’s logos is subject to Altair's
# trademark licensing policies.

from tests.interfaces import *


@tags('multisched')
class TestNodePartition(TestInterfaces):
    """
    Test suite to test partition attr for node
    """
    host_name = socket.gethostname()

    def set_node_partition_attr(self, mgr_cmd="MGR_CMD_SET", n_name=host_name,
                                partition="P1"):
        """
        Common function to set partition attribute to node object
        :param mgr_cmd: qmgr "MGR_CMD_SET/MGR_CMD_UNSET" cmd,
        Defaults to MGR_CMD_SET
        :type mgr_cmd: str
        :param n_name: name of the node/vnode, Defaults to "hostname of server"
        :type n_name: str
        :param partition: "partition" attribute of node object,
                    Defaults to "P1"
        :type partition: str
        """
        attr = {'partition': partition}
        if mgr_cmd is "MGR_CMD_SET":
            self.server.manager(MGR_CMD_SET, NODE, attr,
                                id=n_name, expect=True)
        elif mgr_cmd is "MGR_CMD_UNSET":
            self.server.manager(MGR_CMD_UNSET, NODE,
                                "partition", id=n_name, expect=True)
        else:
            msg = ("Error: test_set_node_partition_attr function takes only "
                   "MGR_CMD_SET/MGR_CMD_UNSET value for mgr_cmd")
            self.logger.error(msg)
            exit()

    def test_set_unset_partition_node_attr(self):
        """
        Test to set/unset the partition attribute of node object
        """
        self.set_node_partition_attr()
        self.set_node_partition_attr(partition="P2")

        # resetting the same partition value
        self.set_node_partition_attr(partition="P2")
        self.set_node_partition_attr(mgr_cmd="MGR_CMD_UNSET")

    def test_set_partition_node_attr_user_permissions(self):
        """
        Test to check the user permissions for set/unset the partition
        attribute of node
        """
        self.set_node_partition_attr()
        msg1 = "Unauthorized Request"
        msg2 = "checking the qmgr error message"
        try:
            self.server.manager(MGR_CMD_SET,
                                NODE, {'partition': "P2"},
                                id=self.host_name, runas=TEST_USER)
        except PbsManagerError as e:
            self.assertTrue(msg1 in e.msg[0], msg2)
        try:
            self.server.manager(MGR_CMD_UNSET,
                                NODE, 'partition',
                                id=self.host_name, runas=TEST_USER)
        except PbsManagerError as e:
            # self.assertEquals(e.rc, 15007)
            # The above code has to be uncommented when the PTL framework
            # bug PP-881 gets fixed
            self.assertTrue(msg1 in e.msg[0], msg2)

    def test_partition_association_with_node_and_queue(self):
        """
        Test to check the set of partition attribute and association
        between queue and node
        """
        attr = {'queue_type': "execution", 'enabled': "True",
                'started': "True", 'partition': "P1"}
        self.server.manager(MGR_CMD_CREATE,
                            QUEUE, attr,
                            id="Q1", expect=True)
        self.set_node_partition_attr()
        attr = {'queue_type': "execution", 'enabled': "True",
                'started': "True", 'partition': "P2"}
        self.server.manager(MGR_CMD_CREATE,
                            QUEUE, attr,
                            id="Q2", expect=True)

        self.set_node_partition_attr(mgr_cmd="MGR_CMD_UNSET")
        self.server.manager(MGR_CMD_SET, NODE, {
                            'queue': "Q2"}, id=self.host_name, expect=True)
        self.set_node_partition_attr(partition="P2")

    def test_mismatch_of_partition_on_node_and_queue(self):
        """
        Test to check the set of partition attribute is disallowed
        if partition ids do not match on queue and node
        """
        self.test_partition_association_with_node_and_queue()
        msg1 = "Invalid partition in queue"
        msg2 = "checking the qmgr error message"
        try:
            self.server.manager(MGR_CMD_SET,
                                QUEUE, {'partition': "P1"},
                                id="Q2")
        except PbsManagerError as e:
            # self.assertEquals(e.rc, 15221)
            # The above code has to be uncommented when the PTL framework
            # bug PP-881 gets fixed
            self.assertTrue(msg1 in e.msg[0], msg2)
        msg1 = "Partition P2 of node is not part of queue"
        try:
            self.server.manager(MGR_CMD_SET,
                                NODE, {'queue': "Q1"},
                                id=self.host_name)
        except PbsManagerError as e:
            # self.assertEquals(e.rc, 15220)
            # The above code has to be uncommented when the PTL framework
            # bug PP-881 gets fixed
            self.assertTrue(msg1 in e.msg[0], msg2)
        msg1 = "Queue Q2 of node is not part of partition"
        try:
            self.set_node_partition_attr()
        except PbsManagerError as e:
            # self.assertEquals(e.rc, 15219)
            # The above code has to be uncommented when the PTL framework
            # bug PP-881 gets fixed
            self.assertTrue(msg1 in e.msg[0], msg2)
