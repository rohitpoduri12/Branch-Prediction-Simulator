#include <iostream>
#include <iomanip>
#include <fstream>
#include <assert.h>
#include <math.h>
#include <unistd.h>
#include "pin.H"
#include <math.h>

typedef unsigned int uint;

KNOB<string> KnobOutputFile(KNOB_MODE_WRITEONCE, "pintool", "o", "bpred.out", "specify file name for branch predictor output");
KNOB<UINT32> KnobM(KNOB_MODE_WRITEONCE, "pintool", "m", "0", "Global history size 0 <= m <= 16");
KNOB<UINT32> KnobN(KNOB_MODE_WRITEONCE, "pintool", "n", "1", "Counter width n >= 1");
KNOB<UINT32> KnobK(KNOB_MODE_WRITEONCE, "pintool", "k", "0", "Branch PC bits to use k, 0 <= k <= 16");

// These variables are my placeholders.  You may replace or modify 
// their usage, as needed.  However the final output format (see Fini, 
// below) should be unchanged.
uint total_bits = 0;
float accuracy = 0;
uint n = 0;
uint m = 0;
uint k = 0;
uint total_branches = 0;    // Represents total number of branch conditions
uint total_taken = 0;       // Represents total number of taken branches
uint total_fallthru = 0;    // Represents total number of not taken branches
uint predicted_correct = 0;  // Represents total number of correctly predicted branch conditions
uint predicted_incorrect = 0;  // Represents total number of incorrectly predicted branch conditions
uint global_history = 0;      // Represents the m-bit global history 
uint address_mask = 0;        //  Represents the mask to be used for generating the last k bits of the branch address
uint address_bit = 0;          // Represents the last k bits of the branch address
uint global_history_mask = 0;  // Represents the mask used for updating the global history
uint  *branch_counter;         // Represents the pointer to the memory location where the branch prediction counters are located
uint counter_max_val = 0;      // Represents the maximum value of the n-bit counter
uint counter_threshold = 0;    // Represents the threshold value of the n-bit counter after which the predicted value is 1 
// Invoked once per dynamic branch instruction
// pc: The address of the branch
// taken: Non zero if a branch is taken
VOID DoBranch(ADDRINT pc, BOOL taken) {

  uint predict;                // This bit stores the predicted value
  
  address_bit = pc&address_mask;    // Obtaining the last k bits of the address
  
  uint ref_value = (global_history<<k) | address_bit;    // The index used for obtaining the appropriate counter
  
  total_branches++;
  
  if(branch_counter[ref_value]>counter_threshold)      // If the counter value is greater than the threshold value the value predicted is 1
    predict = 1;
  
  else
    predict = 0;
    
  global_history = (global_history<<1)&global_history_mask;    // Left shift the global history by 1 position and preserve the number of bits
  
  if(predict==taken)            // If the predicted value equals the actual branch taken value
      predicted_correct++;                                                                                                                                                                                                                                         
  if (taken)
  {
    total_taken++;
    global_history |= 1;                              // Make the global history's LSB 1 as the branch has been taken 
    if(branch_counter[ref_value]!= counter_max_val)    // Update the branch predictor counter according to the saturation counter logic
      branch_counter[ref_value]++;
  }
  else
  {
    total_fallthru++;                  // Update the branch predictor counter according to the saturation counter logic
    if(branch_counter[ref_value]!=0)
      branch_counter[ref_value]--;
  }
    
    
}

// Called once per runtime image load
VOID Image(IMG img, VOID * v) {
  // find and instrument branches
  for (SEC sec = IMG_SecHead(img); SEC_Valid(sec); sec = SEC_Next(sec)) {
    for (RTN rtn = SEC_RtnHead(sec); RTN_Valid(rtn); rtn = RTN_Next(rtn)) {
      RTN_Open(rtn);
      for (INS ins = RTN_InsHead(rtn); INS_Valid(ins); ins = INS_Next(ins)) {
	if (INS_IsBranch(ins) && INS_HasFallThrough(ins)) {
	  INS_InsertCall( ins, IPOINT_BEFORE, (AFUNPTR)DoBranch, IARG_INST_PTR, IARG_BRANCH_TAKEN, IARG_END);
	}
      }
      RTN_Close(rtn);
    }
  }
}

INT32 Usage() {
  cerr << "This pin tool simulates an (m,n,k) branch predictor." << endl;
  cerr << KNOB_BASE::StringKnobSummary();
  cerr << endl;
  return -1;
}

// Called once upon program exit
VOID Fini(int, VOID * v) {

    string filename;
    std::ofstream out;
    filename =  KnobOutputFile.Value();

    out.open(filename.c_str());
    out << "m: " << KnobM.Value() << endl;
    out << "n: " << KnobN.Value() << endl;
    out << "k: " << KnobK.Value() << endl;
    out << "total_branches: " << total_branches << endl;
    out << "total_taken: " << total_taken << endl;
    out << "total_fallthru: " << total_fallthru << endl;
    out << "total_bits: " << total_bits << endl;
    out << "accuracy: " << setprecision(3) << (float)predicted_correct*100/total_branches << endl;    // Accuracy is the percentage of correct predictions/total branch conditions
    out.close();
}

// Called once prior to program execution
int main(int argc, CHAR *argv[]) {
    
    PIN_InitSymbols();

    if (PIN_Init(argc, argv)) {
        return Usage();
    }
    
    n = KnobN.Value();          // Apply assertions on m, n, k
    assert(n >= 1 && n <= 64);
    m = KnobM.Value();
    assert(m >= 0 && m <= 16);
    k = KnobK.Value();
    assert(k >= 0 && k <= 16);
    
    total_bits = (1<<(m+k))*n;        // Total bits are (2^(m+k))*n
    
    address_mask = (1<<k) - 1;        // Address mask is (2^k) - 1
    
    global_history_mask = (1<<m) - 1;   // Global history mask is (2^m) - 1
    
    counter_max_val = (1<<n) - 1;      // Maximim value of counter is (2^n) - 1
    
    counter_threshold = counter_max_val/2;    // Threshold value is the half of the maximum counter value
    
    long double memory_range = 1<<(m+k+1);    // Memory locations required = 2^(m+k) for safe side used (m+k+1)
    
    branch_counter = (uint *)malloc(sizeof(uint)*(memory_range));    // Dynamically allocated the memory
    
    int i;
    
    for(i=0; i < memory_range; i++) {    // Initialized the dynamically allocated memory values to 0
      branch_counter[i] = 0;
    }

    IMG_AddInstrumentFunction(Image, 0);
    PIN_AddFiniFunction(Fini, 0);

    PIN_StartProgram();

    free(branch_counter);  // Free the memory after the code has been executed

    return 0;
}

