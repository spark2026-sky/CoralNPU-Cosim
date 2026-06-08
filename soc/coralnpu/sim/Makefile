# SPDX-License-Identifier: GPL-2.0-only
#
# Copyright (c) 2026 Bright Spark
# Copyright (c) 2020 Xilinx Inc.
# Written by Edgar E. Iglesias,
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
# THE SOFTWARE.

-include ../../../.config.mk
include ./Rules.mk

VFLAGS =-Wno-fatal --pins-bv 2 --sc
VFLAGS += --unroll-count 100000
VFLAGS += --unroll-stmts 100000
VFLAGS += --output-split-cfuncs 500
# FIXME: These should really go away
VFLAGS += -Wno-PINMISSING -Wno-COMBDLY

VM_SC=1

# Enable tracing of internal signals with +trace
VM_TRACE ?= 0

# Trade optimization for compile-time.
OPT_FAST ?= -O2 -fno-stack-protector -fno-var-tracking-assignments
OPT_SLOW ?= -O1 -fstrict-aliasing -fno-var-tracking-assignments
export OPT_FAST
export OPT_SLOW

PCIE_EP_TOP = pcie_ep

CPPFLAGS += -I $(VOBJ_DIR)
CPPFLAGS += -I $(VERILATOR_ROOT)/include

CPPFLAGS += -I ../../../ -I ../../ -I .
CPPFLAGS += -I ../../../libremote-port/
CPPFLAGS += -I ../../../tests/
CXXFLAGS += -Wall -O3 -g

OBJS_COMMON += ../../../tests/test-modules/memory.o
OBJS_COMMON += ../../../trace/trace.o
OBJS_COMMON += ../../../soc/pci/core/pci-device-base.o
OBJS_COMMON += ../../../libremote-port/safeio.o
OBJS_COMMON += ../../../libremote-port/remote-port-proto.o
OBJS_COMMON += ../../../libremote-port/remote-port-sk.o
OBJS_COMMON += ../../../libremote-port/remote-port-tlm.o
OBJS_COMMON += ../../../libremote-port/remote-port-tlm-wires.o
OBJS_COMMON += ../../../libremote-port/remote-port-tlm-memory-master.o
OBJS_COMMON += ../../../libremote-port/remote-port-tlm-memory-slave.o
OBJS_COMMON += ../../../libremote-port/remote-port-tlm-pci-ep.o
OBJS_COMMON += ../../../libremote-port/remote-port-tlm-ats.o

RTL_BRIDGE_PCIE_DIR = ../../../rtl-bridges/pcie-host/pcie/rtl/
RTL_BRIDGE_AXI_DIR = ../../../rtl-bridges/pcie-host/axi/rtl/

include $(RTL_BRIDGE_PCIE_DIR)/ep/files.mk
include $(RTL_BRIDGE_AXI_DIR)/common/files-bridge-axi-common.mk
include $(RTL_BRIDGE_AXI_DIR)/master/files-bridge-axi.mk
include $(RTL_BRIDGE_AXI_DIR)/slave/files-bridge-axi.mk

RTL_BRIDGE_PCIE_EP_LIB = $(VOBJ_DIR)/V$(PCIE_EP_TOP)__ALL.a

CORAL_NPU_TOP = CoralNPU_Top
RTL_CORAL_NPU_DIR = ../../../soc/coralnpu/rtl/
RTL_CORAL_NPU_V = $(RTL_CORAL_NPU_DIR)/CoralNPU_Top.v
RTL_CORAL_NPU_LIB = $(VOBJ_DIR)/V$(CORAL_NPU_TOP)__ALL.a

V_LDLIBS += $(RTL_BRIDGE_PCIE_EP_LIB)
V_LDLIBS += $(RTL_CORAL_NPU_LIB)

VFLAGS_RTL_BRIDGE_PCIE_EP +=-I$(RTL_BRIDGE_PCIE_DIR)/ep/
VFLAGS_RTL_BRIDGE_PCIE_EP +=-I$(RTL_BRIDGE_AXI_DIR)/common/
VFLAGS_RTL_BRIDGE_PCIE_EP +=-I$(RTL_BRIDGE_AXI_DIR)/master/
VFLAGS_RTL_BRIDGE_PCIE_EP +=-I$(RTL_BRIDGE_AXI_DIR)/slave/

REFDESIGN_SIM_OBJS += coralnpu-sim.o
REFDESIGN_SIM_OBJS += $(VERILATED_O)

ifeq "$(VM_TRACE)" "1"
VFLAGS += --trace
VERILATED_OBJS_COMMON += verilated_vcd_c.o
VERILATED_OBJS_COMMON += verilated_vcd_sc.o
CXXFLAGS += -DVM_TRACE=1
endif

VERILATED_OBJS_COMMON += $(OBJS_COMMON)

ALL_OBJS += $(VERILATED_OBJS_COMMON)

TARGETS += coralnpu-sim

################################################################################

all: $(TARGETS)

## Dep generation ##
-include $(ALL_OBJS:.o=.d)

include $(VERILATOR_ROOT)/include/verilated.mk

$(VERILATED_O) $(RTL_BRIDGE_PCIE_EP_LIB): $(RTL_BRIDGE_PCIE_EP_V)
	$(VENV) $(VERILATOR) $(VFLAGS) $(VFLAGS_RTL_BRIDGE_PCIE_EP) $^
	$(MAKE) -C $(VOBJ_DIR) CXXFLAGS="$(CXXFLAGS)" -f V$(PCIE_EP_TOP).mk
	$(MAKE) -C $(VOBJ_DIR) CXXFLAGS="$(CXXFLAGS)" -f V$(PCIE_EP_TOP).mk $(BARE_VERILATED_O)

$(VERILATED_O) $(RTL_CORAL_NPU_LIB): $(RTL_CORAL_NPU_V)
	$(VENV) $(VERILATOR) $(VFLAGS) $(VFLAGS_RTL_BRIDGE_PCIE_EP) $^
	$(MAKE) -C $(VOBJ_DIR) CXXFLAGS="$(CXXFLAGS)" -f V$(CORAL_NPU_TOP).mk
	$(MAKE) -C $(VOBJ_DIR) CXXFLAGS="$(CXXFLAGS)" -f V$(CORAL_NPU_TOP).mk $(BARE_VERILATED_O)

coralnpu-sim.o: $(RTL_BRIDGE_PCIE_EP_LIB) $(RTL_CORAL_NPU_LIB)

coralnpu-sim: $(REFDESIGN_SIM_OBJS) $(VERILATED_OBJS_COMMON) $(V_LDLIBS)
	$(LINK.cc) $^ $(LDLIBS) -o $@

clean:
	$(RM) $(ALL_OBJS) $(ALL_OBJS:.o=.d)
	$(RM) $(TARGETS:=.o) $(TARGETS:=.d)
	$(RM) $(TARGETS:=.vcd)
	$(RM) npu.vcd
	$(RM) -r $(VOBJ_DIR)
	$(RM) $(TARGETS)
