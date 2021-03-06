// Copyright 2018 Robert Balas
// Copyright and related rights are licensed under the Solderpad Hardware
// License, Version 0.51 (the "License"); you may not use this file except in
// compliance with the License.  You may obtain a copy of the License at
// http://solderpad.org/licenses/SHL-0.51. Unless required by applicable law
// or agreed to in writing, software, hardware and materials distributed under
// this License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for the
// specific language governing permissions and limitations under the License.
//
// Author: Robert Balas (balasr@student.ethz.ch)
// Description: Push stimuli to duv

import trdb_pkg::*;
import trdb_tb_pkg::*;

class Driver;

    virtual trace_debugger_if duv_if;
    mailbox #(Stimuli) mail;

    function new(virtual trace_debugger_if duv_if, mailbox #(Stimuli) mail);
        this.duv_if = duv_if;
        this.mail   = mail;
    endfunction


    // parse stimuli file
    function int parse_stimuli(string path, Stimuli stimuli);
        string line;
        int    fp, c;
        string err_str;
        int    linecnt = 0;

        logic                ivalid;
        logic                iexception;
        logic                interrupt;
        logic [CAUSELEN-1:0] cause;
        logic [XLEN-1:0]     tval;
        logic [PRIVLEN-1:0]  priv;
        logic [XLEN-1:0]     iaddr;
        logic [ILEN-1:0]     instr;
        logic                compressed;

        if(DEBUG)
            $display("[STIMULI]@%t: Opening file %s.", $time, path);
        fp = $fopen(stimuli_path, "r");

        if($ferror(fp, err_str)) begin
            $display(err_str);
            return -1;
        end


        while ($fgets(line, fp) != 0) begin
            c = $sscanf(line,"valid=%h exception=%h interrupt=%h cause=%h \
                    tval=%h priv=%h compressed=%h addr=%h instr=%h",
                    ivalid, iexception, interrupt, cause,
                    tval, priv, compressed, iaddr, instr);
            linecnt++;
            stimuli.ivalid.push_front(ivalid);
            stimuli.iexception.push_front(iexception);
            stimuli.interrupt.push_front(interrupt);
            stimuli.cause.push_front(cause);
            stimuli.tval.push_front(tval);
            stimuli.priv.push_front(priv);
            stimuli.iaddr.push_front(iaddr);
            stimuli.instr.push_front(instr);
            stimuli.compressed.push_front(compressed);
        end

        if(DEBUG) begin
            $display("[STIMULI]@%t: Read %d lines.", $time, linecnt);
        end

        if ($ferror(fp, err_str)) begin
            $display("[ERROR]: %s", err_str);
            return -1;
        end else if ($feof(fp)) begin
            if(DEBUG)
                $display("[STIMULI]@%t: Finished parsing %s.", $time, path);
        end

        if(!fp)
            $fclose(fp);
        return 0;
    endfunction


    function void apply_zero();
        if(DEBUG)
            $display("[DRIVER] @%t: Applying zero stimuli.", $time);
        this.duv_if.ivalid     = 1'b0;
        this.duv_if.iexception = 1'b0;
        this.duv_if.interrupt  = 1'b0;
        this.duv_if.cause      = 5'b0;
        this.duv_if.tval       = '0;
        this.duv_if.priv       = 3'h7; // PRIVLEN'h7;
        this.duv_if.iaddr      = '0;
        this.duv_if.instr      = '0;
        this.duv_if.compressed = 1'b0;
    endfunction


    task write_apb(input logic [3:0] waddr, input logic [31:0] wdata);
        // write request
        @(posedge this.duv_if.clk_i);
        this.duv_if.apb_bus.paddr  = waddr;
        this.duv_if.apb_bus.pwrite = 1'b1;
        this.duv_if.apb_bus.psel   = 1'b1;
        this.duv_if.apb_bus.pwdata = wdata;

        @(posedge this.duv_if.clk_i);
        // we are now the access state of the apb
        this.duv_if.apb_bus.penable     = 1'b1;

        // wait until request consumed
        @(posedge this.duv_if.clk_i);
        wait(this.duv_if.apb_bus.pready == 1);
        this.duv_if.apb_bus.penable     = 1'b0;
        this.duv_if.apb_bus.psel   = 1'b0;
    endtask


    task test_sw_dump();
        write_apb(REG_TRDB_DUMP, 32'ha0a0a0a0);
        write_apb(REG_TRDB_DUMP, 32'hb0b0b0b0);
        write_apb(REG_TRDB_DUMP, 32'hc0c0c0c0);
        write_apb(REG_TRDB_DUMP, 32'hd0d0d0d0);
        write_apb(REG_TRDB_DUMP, 32'hf0f0f0f0);
    endtask


    task run(ref logic tb_eos);
        Stimuli stimuli;
        tb_eos = 1'b0;

        if(DEBUG)
            $display("[DRIVER] @%t: Entering run() task.", $time);

        wait(this.duv_if.rst_ni == 1);
            if(DEBUG)
                $display("[DRIVER] @%t: Reset low.", $time);


        // parse stimuli file
        stimuli = new();
        if(parse_stimuli(stimuli_path, stimuli))
            $display("[ERROR]: parse_stimuli() failed.");

        // send to scoreboard
        mail.put(stimuli);

        //TODO: fix this hack
        apply_zero();

        @(posedge this.duv_if.clk_i);
        apply_zero();

        // enable trace debugger
        write_apb(REG_TRDB_CTRL, (1 << TRDB_ENABLE)
                  | (1 << TRDB_TRACE_ACTIVATED)
                  | (FULL_ADDRESS << TRDB_FULL_ADDR)
                  | (IMPLICIT_RET << TRDB_IMPLICIT_RET));

        // apply stimuli according to Top-Down Digital VLSI Design (Kaeslin)
        for(int i = stimuli.ivalid.size() - 1; i >= 0; i--) begin

            @(posedge this.duv_if.clk_i);
            #STIM_APPLICATION_DEL;

            // apply stimuli pop_back()
            this.duv_if.ivalid     = stimuli.ivalid[i];
            this.duv_if.iexception = stimuli.iexception[i];
            this.duv_if.interrupt  = stimuli.interrupt[i];
            this.duv_if.cause      = stimuli.cause[i];
            this.duv_if.tval       = stimuli.tval[i];
            this.duv_if.priv       = stimuli.priv[i];
            this.duv_if.iaddr      = stimuli.iaddr[i];
            this.duv_if.instr      = stimuli.instr[i];
            this.duv_if.compressed = stimuli.compressed[i];

            if(VERBOSE)
                $display("[DRIVER] @%t: Applying vector with addr=%h.", $time,
                         this.duv_if.iaddr);

            #(RESP_ACQUISITION_DEL - STIM_APPLICATION_DEL);
            // take response in monitor.svh
        end

        $display("[DRIVER] @%t: Testing software writes", $time);
        test_sw_dump();

        $display("[DRIVER] @%t: Flushing buffers.", $time);
        // write flush command to register
        write_apb(REG_TRDB_CTRL, (1 << TRDB_FLUSH_STREAM | 3));


        $display("[DRIVER] @%t: Driver finished.", $time);

        @(posedge this.duv_if.clk_i);
        #STIM_APPLICATION_DEL;
        apply_zero();

        @(posedge this.duv_if.clk_i);
        #STIM_APPLICATION_DEL;
        apply_zero();

        @(posedge this.duv_if.clk_i);
        #STIM_APPLICATION_DEL;
        apply_zero();

        tb_eos = 1'b1;

    endtask;


endclass // Driver
