// SPDX-License-Identifier: GPL-2.0-only
/*
 * Top level of QEMU PCIe NPU cosim example.
 *
 * Copyright (c) 2026 Bright Spark
 * Copyright (c) 2020 Xilinx Inc.
 * Written by Edgar E. Iglesias
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#define SC_INCLUDE_DYNAMIC_PROCESSES

#include <inttypes.h>
#include <stdio.h>
#include <signal.h>
#include <unistd.h>

#include "systemc.h"
#include "tlm_utils/simple_initiator_socket.h"
#include "tlm_utils/simple_target_socket.h"
#include "tlm_utils/tlm_quantumkeeper.h"

#include "trace/trace.h"
#include "test-modules/memory.h"
#include "test-modules/signals-axi.h"
#include "test-modules/signals-axilite.h"

#include "utils/bitops.h"

#include "tlm-bridges/tlm2axi-bridge.h"
#include "tlm-bridges/tlm2axilite-bridge.h"
#include "tlm-bridges/axi2tlm-bridge.h"
#include "tlm-bridges/axilite2tlm-bridge.h"
#include "rtl-bridges/pcie-host/axi/tlm/tlm2axi-hw-bridge.h"

#include "remote-port-tlm-pci-ep.h"
#include "soc/interconnect/iconnect.h"
#include "soc/dma/xilinx-cdma.h"
#include "soc/pci/xilinx/xdma-rtl.h"

#include "Vpcie_ep.h"
#include "VCoralNPU_Top.h"

#if VM_TRACE
#include <verilated_vcd_sc.h>
#endif

using namespace sc_core;
using namespace sc_dt;
using namespace std;

class npu_wrapper : public sc_module
{
public:
	sc_in<bool> clk;
	sc_in<bool> rst_n; // Active low.

	axi2tlm_bridge<32, 128, 16, 8, 1, 32, 32, 32, 32, 32 > m_axi_usr_tlm_bridge;
	tlm2axi_bridge<32, 128, 16, 8, 1, 32, 32, 32, 32, 32 > s_axi_usr_tlm_bridge;

	sc_vector<AXISignals<32, 128, 16, 8, 1, 32, 32, 32, 32, 32 > > signals_m_axi_usr;
	sc_vector<AXISignals<32, 128, 16, 8, 1, 32, 32, 32, 32, 32 > > signals_s_axi_usr;

	VCoralNPU_Top npu;
public:

#if VM_TRACE
	VerilatedVcdSc *trace_fp = NULL;

	void trace_on()
	{
		Verilated::traceEverOn(true);
		sc_start(sc_core::SC_ZERO_TIME);
		if (trace_fp) {
			npu.trace(trace_fp, 99); // Trace 99 levels of hierarchy
			trace_fp->open("npu.vcd");
		}
	}

	void trace_off()
	{
		if (trace_fp)
			trace_fp->close();
	}
#endif

	npu_wrapper(sc_core::sc_module_name name) :
		sc_module(name),
		clk("clk"),
		rst_n("rst_n"),
		npu("npu"),
		m_axi_usr_tlm_bridge("m_axi_usr_tlm_bridge"),
		s_axi_usr_tlm_bridge("s_axi_usr_tlm_bridge"),
		signals_m_axi_usr("m_axi_usr", 1),
		signals_s_axi_usr("s_axi_usr", 1)
	{

		npu.clk(clk);
		m_axi_usr_tlm_bridge.clk(clk);
		s_axi_usr_tlm_bridge.clk(clk);

		npu.resetn(rst_n),
		m_axi_usr_tlm_bridge.resetn(rst_n);
		s_axi_usr_tlm_bridge.resetn(rst_n);

		signals_m_axi_usr[0].connect(npu, "m_axi_usr_0_");
		signals_m_axi_usr[0].connect(m_axi_usr_tlm_bridge);

		signals_s_axi_usr[0].connect(npu, "s_axi_usr_0_");
		signals_s_axi_usr[0].connect(s_axi_usr_tlm_bridge);

#if VM_TRACE
		trace_fp = new VerilatedVcdSc;
#endif

	}
};

// This will take care of tiying off unconnected stuff.
SC_MODULE(pcie_bridge_wrapper)
{
public:
	Vpcie_ep ep_bridge;
	sc_in<bool> clk;
	sc_in<bool> rst;
	sc_in<bool> rst_n;

	sc_vector<AXISignals<32, 128, 16, 8, 1, 32, 32, 32, 32, 32 > > signals_m_tieoff;
	sc_vector<AXISignals<32, 128, 16, 8, 1, 32, 32, 32, 32, 32 > > signals_s_tieoff;
	sc_vector<AXISignals<32, 128, 16, 8, 1, 32, 32, 32, 32, 32 > > signals_pcie_m_tieoff;
	sc_vector<AXISignals<32, 128, 16, 8, 1, 32, 32, 32, 32, 32 > > signals_pcie_s_tieoff;
	sc_vector<AXILiteSignals<32, 32 > > signals_pcie_sm_tieoff;
	sc_vector<AXILiteSignals<32, 32 > > signals_pcie_ss_tieoff;

	sc_signal<sc_bv<256> > h2c_gpio_out;
	sc_signal<sc_bv<256> > c2h_gpio_in;

	sc_signal<bool> usr_resetn;

	pcie_bridge_wrapper(sc_module_name name)
		: sc_module(name),
		  ep_bridge("ep_bridge"),
		  clk("clk"),
		  rst("rst"),
		  rst_n("rst_n"),
		  signals_m_tieoff("signals-master-tieoff", 5),
		  signals_s_tieoff("signals-slave-tieoff", 5),
		  signals_pcie_m_tieoff("signals-pcie-master-tieoff", signals_m_tieoff.size()),
		  signals_pcie_s_tieoff("signals-pcie-slave-tieoff", signals_s_tieoff.size()),
		  signals_pcie_sm_tieoff("signals-pcie-s-master-tieoff", signals_m_tieoff.size()),
		  signals_pcie_ss_tieoff("signals-pcie-s-slave-tieoff", signals_s_tieoff.size()),
		  h2c_gpio_out("h2c_gpio_out"),
		  c2h_gpio_in("c2h_gpio_in"),
		  usr_resetn("usr_resetn")

	{
		char pname[128];
		int i;

                assert(signals_m_tieoff.size() <= 6);
                for (i = 0; i < signals_m_tieoff.size(); i++) {
                        int bi = 6 - signals_m_tieoff.size() + i;

                        snprintf(pname, sizeof(pname) - 1, "m_axi_usr_%d_", bi);
                        signals_m_tieoff[i].connect(ep_bridge, pname);

                        snprintf(pname, sizeof(pname) - 1, "m_axi_pcie_m%d_", bi);
                        signals_pcie_m_tieoff[i].connect(ep_bridge, pname);

                        snprintf(pname, sizeof(pname) - 1, "s_axi_pcie_m%d_", bi);
                        signals_pcie_sm_tieoff[i].connect(ep_bridge, pname);
                }

                assert(signals_s_tieoff.size() <= 6);
                for (i = 0; i < signals_s_tieoff.size(); i++) {
                        int bi = 6 - signals_s_tieoff.size() + i;

                        snprintf(pname, sizeof(pname) - 1, "s_axi_usr_%d_", bi);
                        signals_s_tieoff[i].connect(ep_bridge, pname);

                        snprintf(pname, sizeof(pname) - 1, "m_axi_pcie_s%d_", bi);
                        signals_pcie_s_tieoff[i].connect(ep_bridge, pname);

                        snprintf(pname, sizeof(pname) - 1, "s_axi_pcie_s%d_", bi);
                        signals_pcie_ss_tieoff[i].connect(ep_bridge, pname);
                }

		ep_bridge.clk(clk);

		ep_bridge.resetn(rst_n);
		ep_bridge.usr_resetn(usr_resetn);

		ep_bridge.h2c_gpio_out(h2c_gpio_out);
		ep_bridge.c2h_gpio_in(c2h_gpio_in);
	}
};

#define NR_IRQ 2
#define NR_USR_IRQ 64
#define RAM_SIZE (64 * 1024)
#define ADDRESS_RANGE (512 * 1024)

SC_MODULE(Top)
{
	SC_HAS_PROCESS(Top);

	sc_clock clk;
	sc_signal<bool> rst;
	sc_signal<bool> rst_n;
	sc_signal<bool> npu_rst;
	sc_signal<bool> npu_rst_n;

	iconnect<3, 3> xdma_ic;
	iconnect<2, 3> dut_ic;

	pcie_bridge_wrapper pcie_bridge;
	npu_wrapper npu_core;
	remoteport_tlm_pci_ep rp_pci_ep;
	xilinx_xdma_rtl<2> xdma;
	memory ram;

	tlm2axilite_bridge<32, 32> m_axib2;
	axi2tlm_bridge<32, 128, 16, 8, 1, 32, 32, 32, 32, 32 > s_axib2;

	axi2tlm_bridge<32, 128, 16, 8, 1, 32, 32, 32, 32, 32 > m_axi_usr_tlm_bridge;
	tlm2axi_bridge<32, 128, 16, 8, 1, 32, 32, 32, 32, 32 > s_axi_usr_tlm_bridge;

	sc_vector<AXILiteSignals<32, 32 > > signals_s_axi_pcie;
	sc_vector<AXISignals<32, 128, 16, 8, 1, 32, 32, 32, 32, 32 > > signals_m_axi_pcie;
	sc_vector<AXISignals<32, 128, 16, 8, 1, 32, 32, 32, 32, 32 > > signals_m_axi_usr;
	sc_vector<AXISignals<32, 128, 16, 8, 1, 32, 32, 32, 32, 32 > > signals_s_axi_usr;

	// XDMA <-> User Logic Level1 (PCIe Bridge)
	sc_signal<sc_bv<NR_IRQ> > signals_irq_req;
	sc_signal<sc_bv<NR_IRQ> > signals_irq_ack;

	// USR L1 (PCIe Bridge) <-> USR L2 (RAM)
	sc_signal<sc_bv<NR_USR_IRQ> > signals_usr_irq_req;
	sc_signal<sc_bv<NR_USR_IRQ> > signals_usr_irq_ack;

	void pull_reset(void) {
		/* Pull the reset signal.  */
		rst.write(true);
		wait(1, SC_US);
		rst.write(false);
	}

	void gen_rst_n(void) {
		rst_n.write(!rst.read());
	}

	void pull_npu_reset(void) {
		/* Pull the reset signal.  */
		npu_rst.write(false);
		npu_rst.write(true);
		wait(4.75, SC_MS);
		npu_rst.write(true);
		npu_rst.write(false);
	}

	void gen_npu_rst_n(void) {
		npu_rst_n.write(!npu_rst.read());
	}

	// Slow clock for faster simulation.
	Top(sc_module_name name, const char *sk_descr, sc_time quantum) :
		sc_module(name),
		clk("clk", sc_time(10, SC_MS)),
		rst("rst"),
		rst_n("rst_n"),
		npu_rst("npu_rst"),
		npu_rst_n("npu_rst_n"),
		xdma_ic("xdma_ic"),
		dut_ic("dut_ic"),
		pcie_bridge("pcie_bridge"),
		npu_core("npu_core"),
		rp_pci_ep("rp_pci_ep", 0, 1, NR_IRQ, sk_descr),
		xdma("xdma"),
		ram("ram", sc_time(1, SC_NS), RAM_SIZE),
		m_axib2("m_axib2"),
		s_axib2("s_axib2"),
		m_axi_usr_tlm_bridge("m_axi_usr_tlm_bridge"),
		s_axi_usr_tlm_bridge("s_axi_usr_tlm_bridge"),
		signals_s_axi_pcie("s_axi_pcie", 2),
		signals_m_axi_pcie("m_axi_pcie", 2),
		signals_m_axi_usr("m_axi_usr", 1),
		signals_s_axi_usr("s_axi_usr", 1),
		signals_irq_req("signals_irq_req"),
		signals_irq_ack("signals_irq_ack"),
		signals_usr_irq_req("signals_usr_irq_req"),
		signals_usr_irq_ack("signals_usr_irq_ack")
	{
		m_qk.set_global_quantum(quantum);

		SC_THREAD(pull_reset);
		SC_METHOD(gen_rst_n);
		sensitive << rst;

		SC_THREAD(pull_npu_reset);
		SC_METHOD(gen_npu_rst_n);
		sensitive << npu_rst;

		// Connect the bridge
		pcie_bridge.clk(clk);
		pcie_bridge.rst(rst);
		pcie_bridge.rst_n(rst_n);

		npu_core.clk(clk);
		npu_core.rst_n(npu_rst_n);

		// Connect the XDMA
		xdma.axi_clk(clk);
		xdma.axi_aresetn(rst_n);
		rp_pci_ep.rst(rst);
		rp_pci_ep.bind(xdma);

		signals_s_axi_pcie[0].connect(pcie_bridge.ep_bridge, "s_axi_pcie_m0_");
		signals_s_axi_pcie[0].connect(xdma.m_axib);
		signals_m_axi_pcie[0].connect(pcie_bridge.ep_bridge, "m_axi_pcie_m0_");
		signals_m_axi_pcie[0].connect(xdma.s_axib);

		signals_s_axi_pcie[1].connect(pcie_bridge.ep_bridge, "s_axi_pcie_s0_");
		signals_s_axi_pcie[1].connect(m_axib2);
		signals_m_axi_pcie[1].connect(pcie_bridge.ep_bridge, "m_axi_pcie_s0_");
		signals_m_axi_pcie[1].connect(s_axib2);

		pcie_bridge.ep_bridge.irq_req(signals_irq_req);
		xdma.usr_irq_req(signals_irq_req);
		pcie_bridge.ep_bridge.irq_ack(signals_irq_ack);
		xdma.usr_irq_ack(signals_irq_ack);

		// Connect the User logic.
		m_axi_usr_tlm_bridge.clk(clk);
		m_axi_usr_tlm_bridge.resetn(rst_n);
		s_axi_usr_tlm_bridge.clk(clk);
		s_axi_usr_tlm_bridge.resetn(rst_n);
		m_axib2.clk(clk);
		m_axib2.resetn(rst_n);
		s_axib2.clk(clk);
		s_axib2.resetn(rst_n);

		pcie_bridge.ep_bridge.usr_irq_req(signals_usr_irq_req);
		pcie_bridge.ep_bridge.usr_irq_ack(signals_usr_irq_ack);

		signals_m_axi_usr[0].connect(pcie_bridge.ep_bridge, "m_axi_usr_0_");
		signals_m_axi_usr[0].connect(m_axi_usr_tlm_bridge);

		signals_s_axi_usr[0].connect(pcie_bridge.ep_bridge, "s_axi_usr_0_");
		signals_s_axi_usr[0].connect(s_axi_usr_tlm_bridge);

		xdma.tlm_m_axib.bind(*(xdma_ic.t_sk[0]));
		xdma.s_axib.socket.bind(*(xdma_ic.t_sk[1]));
		s_axib2.socket.bind(*(xdma_ic.t_sk[2]));

		m_axi_usr_tlm_bridge.socket.bind(*(dut_ic.t_sk[0]));
		npu_core.m_axi_usr_tlm_bridge.socket.bind(*(dut_ic.t_sk[1]));

		xdma_ic.memmap(0x0, 0x20000 - 1, ADDRMODE_RELATIVE, -1,
				xdma.m_axib.tgt_socket);
		xdma_ic.memmap(0x20000, 0x20000 - 1, ADDRMODE_RELATIVE, -1,
				m_axib2.tgt_socket);
		xdma_ic.memmap(0x0, 0xFFFFFFFFFFFFFFFF - 1, ADDRMODE_RELATIVE, -1,
				xdma.tlm_s_axib);

		dut_ic.memmap(0xb0000000, ADDRESS_RANGE - 1, ADDRMODE_RELATIVE, -1,
				npu_core.s_axi_usr_tlm_bridge.tgt_socket);
		dut_ic.memmap(0xb0100000, 0x30, ADDRMODE_RELATIVE, -1,
				ram.socket);
		dut_ic.memmap(0x0, 0xFFFFFFFFFFFFFFFF - 1, ADDRMODE_RELATIVE, -1,
				s_axi_usr_tlm_bridge.tgt_socket);
	}

private:
	tlm_utils::tlm_quantumkeeper m_qk;
};

void usage(void)
{
	cout << "tlm socket-path sync-quantum-ns" << endl;
}

int sc_main(int argc, char* argv[])
{
	Top *top;
	uint64_t sync_quantum;
#if VM_TRACE
	sc_trace_file *trace_fp = NULL;
#endif

	if (argc < 3) {
		sync_quantum = 10000;
	} else {
		sync_quantum = strtoull(argv[2], NULL, 10);
	}

	sc_set_time_resolution(1, SC_PS);

	top = new Top("top", argv[1], sc_time((double) sync_quantum, SC_NS));

	if (argc < 3) {
		sc_start(1, SC_PS);
		sc_stop();
		usage();
		exit(EXIT_FAILURE);
	}

#if VM_TRACE
	trace_fp = sc_create_vcd_trace_file(argv[0]);
	if (trace_fp)
		trace(trace_fp, *top, top->name());
	top->npu_core.trace_on();
#endif
	sc_start();

#if VM_TRACE
	if (trace_fp) {
		sc_close_vcd_trace_file(trace_fp);
	}
	top->npu_core.trace_off();
#endif
	return 0;
}
