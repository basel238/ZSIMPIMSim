/** $lic$
 * Copyright (C) 2012-2015 by Massachusetts Institute of Technology
 * Copyright (C) 2010-2013 by The Board of Trustees of Stanford University
 *
 * This file is part of zsim.
 *
 * zsim is free software; you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software
 * Foundation, version 2.
 *
 * If you use this software in your research, we request that you reference
 * the zsim paper ("ZSim: Fast and Accurate Microarchitectural Simulation of
 * Thousand-Core Systems", Sanchez and Kozyrakis, ISCA-40, June 2013) as the
 * source of the simulator in any publications that use this software, and that
 * you send us a citation of your work.
 *
 * zsim is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "dramsim_mem_ctrl.h"
#include <map>
#include <string>
#include "event_recorder.h"
#include "tick_event.h"
#include "timing_event.h"
#include "zsim.h"

#ifdef _WITH_DRAMSIM_ // was compiled with dramsim
// #include "DRAMSim.h"

using namespace DRAMSim; // NOLINT(build/namespaces)

class DRAMSimAccEvent : public TimingEvent
{
private:
    DRAMSimMemory *dram;
    bool write;
    Address addr;
    BurstType *bst;

public:
    uint64_t sCycle;

    DRAMSimAccEvent(DRAMSimMemory *_dram, bool _write, Address _addr, int32_t domain, BurstType *_bst) : TimingEvent(0, 0, domain), dram(_dram), write(_write), addr(_addr), bst(_bst) {}
    //        DRAMSimAccEvent(DRAMSimMemory* _dram, bool _write, Address _addr, int32_t domain) :  TimingEvent(0, 0, domain), dram(_dram), write(_write), addr(_addr) {}

    bool isWrite() const
    {
        return write;
    }

    Address getAddr() const
    {
        return addr;
    }

    BurstType *get_bst()
    {
        return bst;
    }

    void simulate(uint64_t startCycle)
    {
        sCycle = startCycle;
        dram->enqueue(this, startCycle);
    }
};

DRAMSimMemory::DRAMSimMemory(string &dramTechIni, string &dramSystemIni, string &outputDir, string &traceName,
                             uint32_t capacityMB, uint64_t cpuFreqHz, uint32_t _minLatency, uint32_t _domain, const g_string &_name, bool _pimMode, KernelType _ktype)
{
    curCycle = 0;
    minLatency = _minLatency;
    // NOTE: this will alloc DRAM on the heap and not the glob_heap, make sure only one process ever handles this
    const char *dramTechIni_c = dramTechIni.c_str();
    const char *dramSystemIni_c = dramSystemIni.c_str();
    const char *outputDir_c = outputDir.c_str();
    const char *traceName_c = traceName.c_str();

    dramCore = getMemorySystemInstance(dramTechIni_c, dramSystemIni_c, outputDir_c, traceName_c, capacityMB, NULL);
    cout << "dramCore pointer: " << dramCore << endl;
    // pim_config = dramCore->get_config();
    dramCore->setCPUClockSpeed(cpuFreqHz);
    std::cerr << "getMemorySystemInstance address: " << (void*) dramCore << std::endl;
    pim_config = dramCore->get_config();
    cerr << "Config in zsim: " << pim_config << endl;

    TransactionCompleteCB *read_cb = new Callback<DRAMSimMemory, void, unsigned, uint64_t, uint64_t>(this, &DRAMSimMemory::DRAM_read_return_cb);
    TransactionCompleteCB *write_cb = new Callback<DRAMSimMemory, void, unsigned, uint64_t, uint64_t>(this, &DRAMSimMemory::DRAM_write_return_cb);
    dramCore->RegisterCallbacks(read_cb, write_cb, nullptr);

    domain = _domain;
    TickEvent<DRAMSimMemory> *tickEv = new TickEvent<DRAMSimMemory>(this, domain);
    tickEv->queue(0); // start the sim at time 0

    name = _name;
    pimMode = _pimMode;
    ktype = _ktype;

    cout << "HERE1" << endl;
    // dramCore->getIniUint("NUM_BANKS", &num_banks_);
    // dramCore->getIniUint("NUM_BANKS", &num_pim_blocks_);
    // dramCore->getIniUint("NUM_BANKS", &num_bank_groups_);
    num_banks_ = pim_config->NUM_BANKS; // getConfigParam(UINT, "NUM_BANKS");
    num_pim_blocks_ = pim_config->NUM_PIM_BLOCKS; //getConfigParam(UINT, "NUM_PIM_BLOCKS");
    num_bank_groups_ = pim_config->NUM_BANK_GROUPS; //getConfigParam(UINT, "NUM_BANK_GROUPS");
cout << "HERE2" << endl;

    cout << "num_banks: " << num_banks_ << ", num_pim_blocks: " << num_pim_blocks_ << ", num_bank_groups: " << num_bank_groups_ << endl;
    // FIXME - sbasel :: hardcoded add to config
    num_grf_ = num_grfA_ = num_grfB_ = 8;
    num_pim_chans_ = 64;
    num_pim_ranks_ = 1;

    num_total_pim_blocks_ = num_pim_blocks_ * num_pim_chans_ * num_pim_ranks_;

    pim_chans_.clear();
    for (int i = 0; i < num_pim_chans_; i++)
        pim_chans_.push_back(i);


    pim_ranks_.clear();
    for (int i = 0; i < num_pim_ranks_; i++)
        pim_ranks_.push_back(i);

    pim_addr_mgr_ = make_shared<PIMAddrManager>(num_pim_chans_, num_pim_ranks_, dramTechIni, dramSystemIni);

    // FIXME - sbasel :: need to move to hooks and not at start of simulation
    if (pimMode == true)
    {
        cout << "here1" << endl;
        vector<PIMCmd> pim_cmds = PIMCmdGen::getPIMCmds(ktype, 0, 0, 0);
        cout << "here2" << endl;
        changePIMMode(dramMode::SB, dramMode::HAB);
        cout << "here3" << endl;
        programCrf(pim_cmds);
        cout << "here4" << endl;
        changePIMMode(dramMode::HAB, dramMode::HAB_PIM);
        cout << "here5" << endl;
    }
    cout << "here6" << endl;
}

void DRAMSimMemory::initStats(AggregateStat *parentStat)
{
    AggregateStat *memStats = new AggregateStat();
    memStats->init(name.c_str(), "Memory controller stats");
    profReads.init("rd", "Read requests");
    memStats->append(&profReads);
    profWrites.init("wr", "Write requests");
    memStats->append(&profWrites);
    profTotalRdLat.init("rdlat", "Total latency experienced by read requests");
    memStats->append(&profTotalRdLat);
    profTotalWrLat.init("wrlat", "Total latency experienced by write requests");
    memStats->append(&profTotalWrLat);
    parentStat->append(memStats);
}

uint64_t DRAMSimMemory::access(MemReq &req)
{
    switch (req.type)
    {
    case PUTS:
    case PUTX:
        *req.state = I;
        break;
    case GETS:
        *req.state = req.is(MemReq::NOEXCL) ? S : E;
        break;
    case GETX:
        *req.state = M;
        break;

    default:
        panic("!?");
    }

    uint64_t respCycle = req.cycle + minLatency;
    assert(respCycle > req.cycle);

    if ((req.type != PUTS /*discard clean writebacks*/) && zinfo->eventRecorders[req.srcId])
    {
        Address addr = req.lineAddr << lineBits;
        bool isWrite = (req.type == PUTX);
        BurstType nullBurst; // FIXME - sbasel :: null for now
        DRAMSimAccEvent *memEv = new (zinfo->eventRecorders[req.srcId]) DRAMSimAccEvent(this, isWrite, addr, domain, &nullBurst);
        // 	 DRAMSimAccEvent* memEv = new (zinfo->eventRecorders[req.srcId]) DRAMSimAccEvent(this, isWrite, addr, domain);
        memEv->setMinStartCycle(req.cycle);
        TimingRecord tr = {addr, req.cycle, respCycle, req.type, memEv, memEv};
        zinfo->eventRecorders[req.srcId]->pushRecord(tr);
    }

    return respCycle;
}

uint32_t DRAMSimMemory::tick(uint64_t cycle)
{
    dramCore->update();
    curCycle++;
    return 1;
}

void DRAMSimMemory::enqueue(DRAMSimAccEvent *ev, uint64_t cycle)
{
    // info("[%s] %s access to %lx added at %ld, %ld inflight reqs", getName(), ev->isWrite()? "Write" : "Read", ev->getAddr(), cycle, inflightRequests.size());
    dramCore->addTransaction(ev->isWrite(), ev->getAddr(), ev->get_bst());
    //    dramCore->addTransaction(ev->isWrite(), ev->getAddr());
    inflightRequests.insert(std::pair<Address, DRAMSimAccEvent *>(ev->getAddr(), ev));
    ev->hold();
}

void DRAMSimMemory::DRAM_read_return_cb(uint32_t id, uint64_t addr, uint64_t memCycle)
{
    std::multimap<uint64_t, DRAMSimAccEvent *>::iterator it = inflightRequests.find(addr);
    assert((it != inflightRequests.end()));
    DRAMSimAccEvent *ev = it->second;

    uint32_t lat = curCycle + 1 - ev->sCycle;
    if (ev->isWrite())
    {
        profWrites.inc();
        profTotalWrLat.inc(lat);
    }
    else
    {
        profReads.inc();
        profTotalRdLat.inc(lat);
    }

    ev->release();
    ev->done(curCycle + 1);
    inflightRequests.erase(it);
    // info("[%s] %s access to %lx DONE at %ld (%ld cycles), %ld inflight reqs", getName(), it->second->isWrite()? "Write" : "Read", it->second->getAddr(), curCycle, curCycle-it->second->sCycle, inflightRequests.size());
}

void DRAMSimMemory::DRAM_write_return_cb(uint32_t id, uint64_t addr, uint64_t memCycle)
{
    // Same as read for now
    DRAM_read_return_cb(id, addr, memCycle);
}

void DRAMSimMemory::changePIMMode(dramMode curMode, dramMode nextMode)
{
    if (curMode == dramMode::SB && nextMode == dramMode::HAB)
    {
        cout << "here21" << endl;
        addTransactionAll(true, 0, 0, pim_abmr_ra, 0x1f, "START_SB_TO_HAB_", &null_bst_);
        cout << "here22" << endl;
        addTransactionAll(true, 0, 1, pim_abmr_ra, 0x1f, &null_bst_);
        cout << "here23" << endl;
        if (num_banks_ >= 2)
        {
            cout << "here24" << endl;
            addTransactionAll(true, 2, 0, pim_abmr_ra, 0x1f, &null_bst_);
            cout << "here25" << endl;
            addTransactionAll(true, 2, 1, pim_abmr_ra, 0x1f, "END_SB_TO_HAB_", &null_bst_);
            cout << "here26" << endl;
        }
        cout << "here27" << endl;
    }
    else if (curMode == dramMode::HAB)
    {
        cout << "here28" << endl;
        if (nextMode == dramMode::SB)
        {
            cout << "here29" << endl;
            addTransactionAll(true, 0, 0, pim_sbmr_ra, 0x1f, "START_HAB_TO_SB", &null_bst_);
            cout << "here210" << endl;
            addTransactionAll(true, 0, 1, pim_sbmr_ra, 0x1f, "END_HAB_TO_SB", &null_bst_);
            cout << "here211" << endl;
        }
        else if (nextMode == dramMode::HAB_PIM)
        {
            cout << "here212" << endl;
            addTransactionAll(true, 0, 0, pim_reg_ra, 0x0, "PIM", &bst_hab_pim_);
            cout << "here213" << endl;
        }
    }
    else if (curMode == dramMode::HAB_PIM && nextMode == dramMode::HAB){
     cout << "here214" << endl;
        addTransactionAll(true, 0, 0, pim_reg_ra, 0x0, "PIM", &bst_hab_);
        cout << "here215" << endl;
    }
    cout << "here216" << endl;

    addBarrier();
    cout << "here217" << endl;
}

void DRAMSimMemory::addTransactionAll(bool is_write, int bg_idx, int bank_idx, int row, int col,
                                      const string tag, BurstType *bst, bool use_barrier, int num_loop)
{
    for (int &ch_idx : pim_chans_)
        for (int &ra_idx : pim_ranks_)
        {
            unsigned local_row = row;
            unsigned local_col = col;
            for (int i = 0; i < num_loop; i++)
            {
                cerr << "here1-1" << endl;
                uint64_t addr = pim_addr_mgr_->addrGenSafe(ch_idx, ra_idx, bg_idx, bank_idx,
                                                           local_row, local_col);
                (tag != "") ? dramCore->addTransaction(is_write, addr, tag, bst)
                            : dramCore->addTransaction(is_write, addr, bst);
                cerr << "Added transaction: " << "is_write - " << is_write << ", addr - " << addr << ", tag - " << tag << ", bst - " << bst << endl;
                local_col++;
            }
        }

    if (use_barrier)
        addBarrier();
}

void DRAMSimMemory::addTransactionAll(bool is_write, int bg_idx, int bank_idx, int row, int col,
                                      BurstType *bst, bool use_barrier, int num_loop)
{
    addTransactionAll(is_write, bg_idx, bank_idx, row, col, "", bst, use_barrier, num_loop);
}

void DRAMSimMemory::addBarrier()
{
    cout << "here218" << endl;
    cout << "num_pim_chans: " << pim_chans_.size() << endl;
    for (int &ch_idx : pim_chans_){
        dramCore->addBarrier(ch_idx);
        cout << "here219, ch_idx: " << ch_idx << endl;
    }
    cout << "here220" << endl;
}

void DRAMSimMemory::programCrf(vector<PIMCmd> &cmds)
{
    PIMCmd nop_cmd(PIMCmdType::NOP, 0);
    for (int i = 0; i < 4; i++)
    {
        if (i * 8 >= cmds.size())
            break;
        crf_bst_[i].set(nop_cmd.toInt(), nop_cmd.toInt(), nop_cmd.toInt(), nop_cmd.toInt(),
                        nop_cmd.toInt(), nop_cmd.toInt(), nop_cmd.toInt(), nop_cmd.toInt());
        for (int j = 0; j < 8; j++)
        {
            if (i * 8 + j >= cmds.size())
                break;
            crf_bst_[i].u32Data_[j] = cmds[i * 8 + j].toInt();
        }
        addTransactionAll(true, 0, 1, pim_reg_ra, 0x4 + i, "PROGRAM_CRF", &(crf_bst_[i]));
    }
    addBarrier();
}

#else // no dramsim, have the class fail when constructed

using std::string;

DRAMSimMemory::DRAMSimMemory(string &dramTechIni, string &dramSystemIni, string &outputDir, string &traceName,
                             uint32_t capacityMB, uint64_t cpuFreqHz, uint32_t _minLatency, uint32_t _domain, const g_string &_name, bool pimMode, KernelType _ktype)
{
    panic("Cannot use DRAMSimMemory, zsim was not compiled with DRAMSim");
}

void DRAMSimMemory::initStats(AggregateStat *parentStat) { panic("???"); }
uint64_t DRAMSimMemory::access(MemReq &req)
{
    panic("???");
    return 0;
}
uint32_t DRAMSimMemory::tick(uint64_t cycle)
{
    panic("???");
    return 0;
}
void DRAMSimMemory::enqueue(DRAMSimAccEvent *ev, uint64_t cycle) { panic("???"); }
void DRAMSimMemory::DRAM_read_return_cb(uint32_t id, uint64_t addr, uint64_t memCycle) { panic("???"); }
void DRAMSimMemory::DRAM_write_return_cb(uint32_t id, uint64_t addr, uint64_t memCycle) { panic("???"); }

#endif
