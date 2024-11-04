/*
Joshua Tlatelpa-Agustin
9/18/24
bf language compiler
written for adv compilers course
*/
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <stack>
#include <map>
#include <unordered_set>
#include <sstream>
#include <cassert>
#include <utility> // for std::pair

#include "llvm/IR/IRBuilder.h"    // For IRBuilder and instruction building
#include "llvm/IR/LLVMContext.h"  // For LLVMContext, which manages LLVM global state
#include "llvm/IR/Module.h"       // For Module, representing a full LLVM program
#include "llvm/IR/BasicBlock.h"   // For BasicBlock, representing code blocks
#include "llvm/IR/Function.h"     // For Function, representing functions in LLVM IR
#include "llvm/IR/Verifier.h"     // For verifying correctness of the generated IR
#include "llvm/IR/Instructions.h" // For specific instructions like PHINode, LoadInst, StoreInst, etc.
#include "llvm/IR/Type.h"

#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/IPO.h>
#include <llvm/Transforms/Utils.h>

#include <llvm/Passes/PassBuilder.h>

using namespace std;
using namespace llvm;

// Cursed...
//  Initialize LLVM components
static std::unique_ptr<LLVMContext> TheContext = std::make_unique<LLVMContext>();
static std::unique_ptr<IRBuilder<>> Builder = std::make_unique<IRBuilder<>>(*TheContext);
static std::unique_ptr<Module> TheModule = std::make_unique<Module>("module", *TheContext);

Function *mainFunction;

stack<PHINode *> phiNodeStack;

Value *middlePtr;

string cell_pointer_var = "middle";

vector<char> program_file;
ofstream *output_file;
int loop_num = -1;
int seek_loop = -1;
stack<int> myStack;
int tape_size = 1048576;

bool simple_loop_flag = false;
bool seek_flag = false;
bool optimization_flag = false;
string bf_file_name = "";

/*
jasm, short for josh asm :] outputs to output_file.
*/
void jasm(string text)
{
    *output_file << text << endl;
}

void print_padding()
{
    *output_file << endl;
}

// Define stacks to manage nested loop blocks
std::stack<llvm::BasicBlock *> loopStartStack;
std::stack<llvm::BasicBlock *> loopCheckStack;
std::stack<llvm::BasicBlock *> afterLoopStack;
std::stack<llvm::BasicBlock *> loopBodyStack;

stack<Value *> tapePointerStack;

std::stack<llvm::BasicBlock *> entryBlockStack;

/*
naive implementation, also easier to debug
*/
void bf_assembler(char token)
{

    // main()
    switch (token)
    {
    case '>':

        // move our pointer to the right,
        {
            Value *newMiddlePtr = Builder->CreateInBoundsGEP(Builder->getInt8Ty(), middlePtr, Builder->getInt32(1), "moveRight");
            middlePtr = newMiddlePtr;
        }
        break;
    case '<':
        // move our pointer to the left,
        {
            middlePtr = Builder->CreateInBoundsGEP(Builder->getInt8Ty(), middlePtr, Builder->getInt32(-1), "moveLeft");
            // produces:   %moveLeft = getelementptr inbounds i8, ptr %moveRight, i32 -1
        }
        break;
    case '+':
    {
        // Load the current value at `middlePtr`
        Value *currentValue = Builder->CreateLoad(Builder->getInt8Ty(), middlePtr, "loadCurrent");

        // Check if current value is 255
        Value *isMax = Builder->CreateICmpEQ(currentValue, Builder->getInt8(255), "isMax");

        // If it's 255, set to 0; otherwise, increment by 1
        Value *incrementedValue = Builder->CreateAdd(currentValue, Builder->getInt8(1), "increment");
        Value *wrappedValue = Builder->CreateSelect(isMax, Builder->getInt8(0), incrementedValue, "wrapAround");

        // Store the result back at `middlePtr`
        Builder->CreateStore(wrappedValue, middlePtr);
    }
    break;

    case '-':
    {
        // Load the current value at `middlePtr`
        Value *currentValue = Builder->CreateLoad(Builder->getInt8Ty(), middlePtr, "loadCurrent");

        // Check if current value is 0
        Value *isZero = Builder->CreateICmpEQ(currentValue, Builder->getInt8(0), "isZero");

        // If it's 0, set to 255; otherwise, decrement by 1
        Value *decrementedValue = Builder->CreateSub(currentValue, Builder->getInt8(1), "decrement");
        Value *wrappedValue = Builder->CreateSelect(isZero, Builder->getInt8(255), decrementedValue, "wrapAround");

        // Store the result back at `middlePtr`
        Builder->CreateStore(wrappedValue, middlePtr);
    }
    break;

    case '.':

        // print what's at the pointer, it's an i8
        //  Load the value to print
        {
            // Load the value to print
            Value *valueToPrint = Builder->CreateLoad(Type::getInt8Ty(*TheContext), middlePtr, "valueToPrint");

            // Create a prototype for putchar (int putchar(int))
            FunctionType *putcharType = FunctionType::get(Type::getInt32Ty(*TheContext), Type::getInt8Ty(*TheContext), false);
            FunctionCallee putcharFunction = TheModule->getOrInsertFunction("putchar", putcharType);

            // Call putchar with the loaded value
            Builder->CreateCall(putcharFunction, valueToPrint);
        }

        break;
    case ',':

    {
        // Create a prototype for getchar (int getchar())
        FunctionType *getcharType = FunctionType::get(Type::getInt32Ty(*TheContext), false);
        FunctionCallee getcharFunction = TheModule->getOrInsertFunction("getchar", getcharType);

        // Call getchar to read a character from stdin
        // Call getchar to read a character from stdin
        Value *inputChar = Builder->CreateCall(getcharFunction, {}, "inputChar");

        // Truncate the returned int32 to int8 if needed (for 8-bit storage)
        Value *truncatedInput = Builder->CreateTrunc(inputChar, Type::getInt8Ty(*TheContext), "truncatedInput");

        // Store the character at the location pointed to by middlePtr
        Builder->CreateStore(truncatedInput, middlePtr);
    }
    break;

    case '[':
    {
        // Create loop blocks and push onto stacks
        BasicBlock *loopStart = BasicBlock::Create(*TheContext, "loop_start", mainFunction);
        BasicBlock *afterLoop = BasicBlock::Create(*TheContext, "after_loop", mainFunction);

        loopStartStack.push(loopStart);
        afterLoopStack.push(afterLoop);

        // Basic block before we enter [

        BasicBlock * entryBlock = Builder->GetInsertBlock();

        

        Builder->SetInsertPoint(entryBlock);
        // Initial loop check
        Builder->CreateBr(loopStart);
        Builder->SetInsertPoint(loopStart);

        PHINode *phiPtr = Builder->CreatePHI(middlePtr->getType(), 2, "middle_phi");
        phiPtr->addIncoming(middlePtr, entryBlock);
        middlePtr = phiPtr;
        phiNodeStack.push(phiPtr);

        break;
    }

    // Handle ']'
    case ']':
    {
        // Finish loop body, branch to loop check
        BasicBlock *loopStart = loopStartStack.top();
        BasicBlock *afterLoop = afterLoopStack.top();

        // Pop from stacks and continue in `afterLoop`
        loopStartStack.pop();
        afterLoopStack.pop();


        PHINode *phiPtr = phiNodeStack.top();
        phiNodeStack.pop();

        phiPtr->addIncoming(middlePtr, Builder->GetInsertBlock());

        // Re-evaluate loop condition
        Value *cellValue = Builder->CreateLoad(Builder->getInt8Ty(), middlePtr, "cell_value");
        Value *isZero = Builder->CreateICmpEQ(cellValue, Builder->getInt8(0), "is_zero");

        // Conditional branch: back to loop_body if non-zero, else exit
        Builder->CreateCondBr(isZero, afterLoop, loopStart);
        Builder->SetInsertPoint(afterLoop);

        ///
        // if (loopStartStack.size() == 0)
        // {
        //     while (entryBlockStack.size() != 0)
        //         entryBlockStack.pop();
        // }

        break;
    }

    default:
        // non bf instruction, so we ignore
        break;
    } // end switch
}

bool startsWith(const std::string &str, const std::string &prefix)
{
    return str.compare(0, prefix.length(), prefix) == 0;
}

void asm_setup()
{
    // Assembly setup
    jasm(".file	\"bf compiler\"");

    jasm(".section .data");

    if (optimization_flag || seek_flag)
    {
        // offset masks= 1,2,4,8,16
        jasm(".p2align 5");
        jasm(".four_offset_mask:");
        jasm(".quad   282578783371521");
        jasm(".quad   282578783371521");
        jasm(".quad   282578783371521");
        jasm(".quad  282578783371521");

        jasm(".p2align 5");
        jasm(".one_offset_mask:");
        jasm(".quad   0");
        jasm(".quad   0");
        jasm(".quad   0");
        jasm(".quad   0");

        jasm(".p2align 5");
        jasm(".two_offset_mask:");
        jasm(".quad   281479271743489");
        jasm(".quad   281479271743489");
        jasm(".quad   281479271743489");
        jasm(".quad  281479271743489");

        jasm(".p2align 5");
        jasm(".eight_offset_mask:");
        jasm(".quad   282578800148737");
        jasm(".quad   282578800148737");
        jasm(".quad   282578800148737");
        jasm(".quad   282578800148737");

        jasm(".p2align 5");
        jasm(".sixteen_offset_mask:");
        jasm(".quad   72340172838076673");
        jasm(".quad   282578800148737");
        jasm(".quad   72340172838076673");
        jasm(".quad  282578800148737");

        jasm(".p2align 5");
        jasm(".thirty_two_offset_mask:");
        jasm(".quad   72340172838076673");
        jasm(".quad   72340172838076673");
        jasm(".quad   72340172838076673");
        jasm(".quad  282578800148737");
    }

    jasm(".text");
    jasm(".section	.text");
    jasm(".globl	main");
    jasm(".type	main, @function");
    print_padding();

    jasm("main:");

    jasm("pushq	%rbp");
    jasm("movq	%rsp, %rbp");
    // Allocate 16 bytes of stack space for local variables
    jasm("subq	$16, %rsp");
    // Allocate 100,000 bytes with malloc
    jasm("movl	$" + to_string(tape_size) + ", %edi");

    jasm("call	malloc@PLT");
    // Store the pointer returned by malloc in the local variable at -8(%rbp)
    jasm("movq	%rax, -8(%rbp)");

    // Calculate the address 50,000 bytes into the allocated memory
    jasm("addq    $" + to_string(tape_size / 2) + ", %rax"); // Add the offset to %rax

    // Store the adjusted pointer back at -8(%rbp)
    jasm("movq    %rax, -8(%rbp)");
    jasm("movq    -8(%rbp), %r13"); // keep our copy of the cell address at r13
}

void asm_cleanup()
{
    // Set the return value to 0 (successful completion)
    jasm("movl    $0, %eax");
    // Proper stack cleanup
    jasm("movq    %rbp, %rsp");
    // Restore the old base pointer
    jasm("popq    %rbp");
    // Return from the function
    jasm("ret");
    print_padding();
}
/*
Turn  vector<char> bf program to vector<string>, to support saving complex instructions
also this list will not contain non-instruction
*/
vector<string> init_optimized_program_list(vector<char> list)
{
    vector<string> return_list;

    for (int i = 0; i < list.size(); i++)
    {
        char token = list[i];
        switch (token)
        {
        case '>':
            return_list.push_back(string(1, token));
            break;
        case '<':
            return_list.push_back(string(1, token));
            break;
        case '+':
            return_list.push_back(string(1, token));
            break;
        case '-':
            return_list.push_back(string(1, token));
            break;
        case '.':
            return_list.push_back(string(1, token));
            break;
        case ',':
            return_list.push_back(string(1, token));
            break;
        case '[':
            return_list.push_back(string(1, token));
            break;
        case ']':
            return_list.push_back(string(1, token));
            break;
        default:
            // non bf instruction, so we ignore
            break;
        } // end switch

    } // end for loop

    return return_list;
}

/*
returns an unordered_set<int> of all starting loop indices.
*/
unordered_set<int> get_loop_indices(vector<string> list)
{
    unordered_set<int> return_set;

    for (int i = 0; i < list.size(); i++)
    {
        string token = list[i];

        if (token == "[")
            return_set.insert(i);
    }
    return return_set;
}

/*
Returns associated loop of given index from passed in list.
ie, [*.>><], given program index of [, will return [*.>><]
*/
vector<string> get_loop_string(int j, vector<string> list)
{
    vector<string> return_list;
    if (list[j] == "[")
    {
        return_list.push_back("[");
        int count = 1;
        while (count != 0)
        {
            j++;

            if (list[j] == "[")
                count++;

            if (list[j] == "]")
                count--;

            // Code for handling valid instr
            return_list.push_back(list[j]);
        }

        return return_list;
    }

    cout << "couldn't find '[' in get_loop_string(), wrong index?? " << endl;
    return return_list;
}

// tells us net change of our initial cell
int get_current_cell_change(vector<string> loop_string)
{
    int cell = 0;
    int cell_value = 0;
    // iterate over body of loop ie -> [...]
    for (int i = 1; i < loop_string.size() - 1; i++)
    {
        string token = loop_string[i];

        if (token == "<")
            cell--;
        if (token == ">")
            cell++;

        if (token == "+")
        {
            if (cell == 0)
            {
                cell_value++;
            }
        }
        if (token == "-")
        {
            if (cell == 0)
            {
                cell_value--;
            }
        }

    } // end for loop

    return cell_value;
}

/*
Checks if input string is a simple loop
*/
bool is_simple_loop(vector<string> loop_string)
{
    bool answer = true;
    int cell_offset = 0;
    int loop_cell_value = 0;
    // iterate over body of loop ie -> [...]
    for (int i = 1; i < loop_string.size() - 1; i++)
    {
        string token = loop_string[i];

        if (token == ",")
        {
            answer = false;
            break;
        }
        if (token == ".")
        {
            answer = false;
            break;
        }
        if (token == "[")
        {
            answer = false;
            break;
        }
        if (token == "]")
        {
            answer = false;
            break;
        }
        if (token == "<")
            cell_offset--;
        if (token == ">")
            cell_offset++;

        if (token == "+")
        {
            if (cell_offset == 0)
            {
                loop_cell_value++;
            }
        }
        if (token == "-")
        {
            if (cell_offset == 0)
            {
                loop_cell_value--;
            }
        }
        if (token.compare(0, 12, "expr_simple:") == 0)
        {
            answer = false;
            break;
        }
        if (token.compare(0, 10, "expr_seek:") == 0)
        {
            answer = false;
            break;
        }

    } // end for loop

    if (cell_offset != 0)
        answer = false;

    if (abs(loop_cell_value) != 1)
        answer = false;

    return answer;
}

/*
power of two or its negative counterpart
*/
bool is_power_of_two(int n)
{
    bool answer = false;
    switch (n)
    {
    case -32:
    case -16:
    case -8:
    case -4:
    case -2:
    case 2:
    case 4:
    case 16:
    case 32:
        answer = true;
        break;
    default:
        break;
    }

    return answer;
}
/*
Checks if input loop is a power of 2 seek loop
*/
int is_power_two_seek_loop(vector<string> loop_string)
{
    int offset = 0;
    // iterate over body of loop ie -> [...]
    for (int i = 1; i < loop_string.size() - 1; i++)
    {
        string token = loop_string[i];

        if (token != ">" && token != "<")
        {
            return false;
        }
        if (token == ">")
            offset++;

        if (token == "<")
            offset--;

    } // end for loop

    if (!is_power_of_two(offset))
        return false;
    // basically only true if we only have one type of seek
    return offset;
}

/*
Checks if input loop is a seek loop
*/
int is_seek_loop(vector<string> loop_string)
{
    int offset = 0;
    // iterate over body of loop ie -> [...]
    for (int i = 1; i < loop_string.size() - 1; i++)
    {
        string token = loop_string[i];

        if (token != ">" && token != "<")
        {
            return false;
        }
        if (token == ">")
            offset++;

        if (token == "<")
            offset--;

    } // end for loop

    // basically only true if we only have one type of seek
    return offset;
}

/*
Prints all elements in a vector<string> with no new line in between elements, with a new line at end.
*/
void print_string_vector(vector<string> list)
{
    for (auto token : list)
        cout << token;

    cout << endl;
}

/*
prints string vector vecrtically and ignores white space
*/
void vprint_string_vector(vector<string> list)
{
    for (auto token : list)
    {
        if (token == " ")
            continue;
        cout << token << endl;
    }

    cout << endl;
}
string expr_dict_to_string(map<int, int> dict, int loop_increment)
{

    string sb = "expr_simple:";

    // will tell us if we have +1 or -1 simple loops.
    if (loop_increment >= 0)
        sb += "+";

    else
        sb += "-";

    for (const auto &pair : dict)
    {
        sb += to_string(pair.first);
        sb += ":";
        sb += to_string(pair.second);
        sb += ",";
    }

    return sb;
}

void print_int_int_map(map<int, int> dict)
{

    for (const auto &pair : dict)
    {
        cout << "{";
        cout << to_string(pair.first);
        cout << ":";
        cout << to_string(pair.second);
        cout << "}";
        cout << endl;
    }
}
vector<string> optimize_simple_loop(int loop_index, int loop_increment, vector<string> loop, vector<string> program)
{

    map<int, int> dict;
    int tape_offset = 0;

    for (auto token : loop)
    {
        if (token == ">")
            tape_offset++;
        if (token == "<")
            tape_offset--;
        if (token == "+")
        {
            if (dict.find(tape_offset) != dict.end())
                dict[tape_offset] = dict[tape_offset] + 1;
            else
                dict[tape_offset] = 0 + 1;
        }
        if (token == "-")
        {
            if (dict.find(tape_offset) != dict.end())
                dict[tape_offset] = dict[tape_offset] - 1;
            else
                dict[tape_offset] = 0 - 1;
        }
    } // end loop

    for (int i = 0; i < loop.size(); i++)
    {
        program[loop_index + i] = " ";
    }
    // remove loop if [], should never fire unless infinite loop
    // if (loop.size() == 2)
    //     return program;

    program[loop_index] = expr_dict_to_string(dict, loop_increment);

    return program;
}

map<int, int> expr_string_to_dict(string expr)
{
    map<int, int> dict;

    // Check if the string starts with "expr_simple:"
    if (expr.compare(0, 12, "expr_simple:") == 0)
    {
        // Remove the prefix, plus the sign attached
        expr.erase(0, 13);
        // cout << expr );
        //  example- 0:-1,1:7,2:10,3:3,4:1,
        std::string first, second;

        std::istringstream ss(expr); // Use stringstream for easy tokenization

        while (std::getline(ss, first, ':'))
        {
            // Extract string before ':'
            std::getline(ss, second, ','); // Extract string after ':' but before ','

            int int_first = stoi(first);
            int int_second = stoi(second);
            if (dict.find(int_first) != dict.end())
            {
                cout << "why was there a duplicate while parsing expr into dict?" << endl;
                cout << int_first << "," << int_second << endl;
                assert(1 == 0);
            }
            else
                dict[int_first] = int_second;
        }

    } // end expr_simple
    else
    {
        cout << "expr_string_to_dict(), you didn't pass in an expr string?" << endl;
        assert(1 == 0);
    }

    return dict;
}

int get_expr_seek_offset(string expr)
{
    int offset = 0;
    if (expr.compare(0, 10, "expr_seek:") == 0)
    {
        // Remove the prefix, plus the sign attached
        expr.erase(0, 10);
        offset = stoi(expr);
        // cout<< offset<<endl;
    }
    else
    {
        cout << "erm u didnt pass in a expr_seek string... in get_expr_seek_offset()" << endl;
        assert(1 == 0);
    }

    return offset;
}

/*
our optimized assembler, usees vector strings to store instructions which allow for easier modification of bf source
we also further optimize by keeping out tape location at register r13
*/

void bf_string_assembler(string token)
{
    // print_padding();

    if (token == ">")
        // add one to pointer address
        jasm("addq    $1, %r13");
    if (token == "<")
        // remove one from pointer address
        jasm("addq    $-1, %r13");
    if (token == "+")
        // Add 1 to the byte
        jasm("addb    $1, (%r13)");

    if (token == "-")
        jasm("subb    $1, (%r13)");

    if (token == ".")
    {

        // Load the byte from the address into %al (to use with putc)
        // jasm("movb    (%r13), %al");

        // Prepare for putc
        // Load file descriptor for stdout into %rsi
        jasm("movq    stdout(%rip), %rsi");
        // Move and sign-extend byte in %al to %edi
        jasm("movsbl  (%r13), %edi");

        // Call putc to print the character
        jasm("call    putc@PLT");

        // cout << tape[tape_position];
    }
    if (token == ",")
    {

        // Move the file pointer for stdin into the %rdi register
        jasm("movq    stdin(%rip), %rdi");

        // Call the getc function to read a character from stdin (returned in %al)
        jasm("call    getc@PLT");
        // Move the byte from %al into %bl

        // Store the byte from %bl into r13 our current cell
        jasm("movb    %al, (%r13)");
    }
    if (token == "[")
    {

        loop_num++;
        myStack.push(loop_num);

        string start_label = "start_loop_" + to_string(loop_num);
        string end_label = "end_loop_" + to_string(loop_num);

        // jump to matching end label if 0
        jasm("cmpb    $0, (%r13)");
        jasm("je      " + end_label);
        jasm(start_label + ":");
    }
    if (token == "]")
    {
        int match_loop = myStack.top();
        myStack.pop();
        string start_label = "start_loop_" + to_string(match_loop);
        string end_label = "end_loop_" + to_string(match_loop);

        // jump to matching start label if not 0
        jasm("cmpb    $0, (%r13)");
        jasm("jne      " + start_label);
        jasm(end_label + ":");
    }

    if (startsWith(token, "expr_simple:"))
    {
        string sign_of_loop;

        // if +, we perform loop 256-255 times
        // if -, we perform full p[0] times
        if (startsWith(token, "expr_simple:+"))
            sign_of_loop = "+";

        if (startsWith(token, "expr_simple:-"))
            sign_of_loop = "-";

        map<int, int> simple_expr = expr_string_to_dict(token);

        // 8 bit regists, AH AL BH BL CH CL DH DL
        // ch and bl work

        // save current cell contents at start of loop in rcx

        if (sign_of_loop == "-")
            jasm("movq    (%r13), %rcx");

        if (sign_of_loop == "+")
        {
            // input can't be more than 255 so we're good
            jasm("movq    $256, %rcx ");
            jasm("subq    (%r13), %rcx  ");
        }

        for (const auto &pair : simple_expr)
        {
            if (pair.first == 0)
                continue;

            // pair.first = pointer offset
            // pair.second = cell +- change per loop

            // copy address of the first loop cell
            jasm("movq   %r13, %r12");
            // adjust pointer by our offset
            jasm("addq   $" + to_string(pair.first) + ", %r12");
            // set our constant change of cell
            jasm("movq    $" + to_string((pair.second)) + ", %r15");
            // constant multiplied by times loop happens
            jasm("imul   %rcx, %r15");

            jasm("addb    %r15b , (%r12)");
        }
        // our loop should always end in 0, this assures it, but would break
        // intentional infinite loops ¯\_(ツ)_/¯, saves us like 5 instr per loop
        jasm("movb    $0, (%r13)");

        print_padding();
    }

    if (startsWith(token, "expr_seek:"))
    {
        print_padding();
        loop_num++;

        int seek_offset = get_expr_seek_offset(token);

        if (is_power_of_two(seek_offset))
        {
            // cout<< seek_offset<<endl;

            string start_label = "start_seek_loop_" + to_string(loop_num);
            string end_label = "end_seek_loop_" + to_string(loop_num);

            // jasm("movb    (%r13), %cl");
            jasm("cmpb    $0, (%r13)");

            jasm("je      " + end_label);

            // this makes our offset masks easier to reason about (i like seeing 32)
            // subtracting 32, so our loop can always add 32, and on first iterations will be 0.

            if (seek_offset > 0)
            {
                jasm("addq $1, %r13");
                jasm("subq    $32, %r13");
            }
            else
            {
                // same alignment reason, but we don't need to offset our first loop since we want to read the previous bytes
                jasm("addq $1, %r13");
                // jasm("addq    $32, %r13");
            }

            if (abs(seek_offset) == 4)
                jasm("vmovdqa .four_offset_mask(%rip), %ymm0");
            if (abs(seek_offset) == 1)
                jasm("vmovdqa .one_offset_mask(%rip), %ymm0");
            if (abs(seek_offset) == 2)
                jasm("vmovdqa .two_offset_mask(%rip), %ymm0");
            if (abs(seek_offset) == 8)
                jasm("vmovdqa .eight_offset_mask(%rip), %ymm0");
            if (abs(seek_offset) == 16)
                jasm("vmovdqa .sixteen_offset_mask(%rip), %ymm0");
            if (abs(seek_offset) == 32)
                jasm("vmovdqa .thirty_two_offset_mask(%rip), %ymm0");

            // Loop for checking bytes in chunks of 32
            ////////////////////////////////////////////////////////////
            jasm(start_label + ":");

            if (seek_offset > 0)
                jasm("addq    $32, %r13");
            else
                jasm("subq    $32, %r13"); // CHANGE IF neg i think

            jasm("vpor    (%r13), %ymm0, %ymm2");
            jasm("vpxor   %xmm1, %xmm1, %xmm1");
            jasm("vpcmpeqb        %ymm1, %ymm2, %ymm2");
            jasm("vpmovmskb       %ymm2, %eax");
            jasm("testl   %eax, %eax");
            jasm("je      " + start_label);

            if (seek_offset > 0)

                jasm("bsfl    %eax, %eax"); // CHANGE IF neg i think

            else
                jasm("bsrl    %eax, %eax");

            jasm("addq %rax, %r13");

            /////////////////////////////////////////////////////////////////
            // save offset

            jasm(end_label + ":");
            print_padding();
        }

        // all other seek loops
        else
        {
            string start_label = "start_seek_loop_" + to_string(loop_num);
            string end_label = "end_seek_loop_" + to_string(loop_num);

            //  Load byte into %cl (lower 8 bits)
            jasm("movb    (%r13), %cl");
            // jump to matching end label if 0
            jasm("cmpb    $0, %cl");
            jasm("je      " + end_label);

            jasm(start_label + ":");

            // remove one from pointer address
            jasm("addq    $" + to_string(seek_offset) + ", %r13");

            // Load byte into %cl (lower 8 bits)
            jasm("movb    (%r13), %cl");
            // jump to matching end label if 0
            jasm("cmpb    $0, %cl");
            jasm("jne      " + start_label);

            jasm(end_label + ":");
        }
    } // end seek

} // end asm_string

vector<string> optimize_seek_loop(int loop_index, int seek_offset, vector<string> loop, vector<string> program)
{
    string sb = "expr_seek:";
    for (int i = 0; i < loop.size(); i++)
    {
        program[loop_index + i] = " ";
    }
    sb += to_string(seek_offset);

    program[loop_index] = sb;
    return program;
}

bool is_simple_loop2(vector<string> loop)
{
    bool answer = true;
    // we do not count the brackets only inside ie [...]
    for (int i = 1; i < loop.size() - 1; i++)
    {
        string t = loop[i];
        if (
            t != ">" &&
            t != "<" &&
            !startsWith(t, "expr_simple:") &&
            t != "+" &&
            t != "-" &&
            t != " ")
            answer = false;
    }

    return answer;
}
int main(int argc, char *argv[])
{
    // setup

    if (argc < 2)
    {
        cout << "No input file?" << endl;
        return 1;
    }
    for (int i = 0; i < argc; i++)
    {
        string args = argv[i];

        if (args == "-O")
            simple_loop_flag = true;
        if (args == "-v")
            seek_flag = true;
        if (args == "-O1")
            optimization_flag = true;
        if (args.find(".b") != std::string::npos)
        {
            assert(bf_file_name == "");

            bf_file_name = args;
        }
    }

    // Open bf file
    ifstream inputFile(bf_file_name);
    if (!inputFile)
    {
        cout << "Couldn't open file: " << bf_file_name << endl;
        return 1;
    }
    // Read the file into a vector of chars
    program_file.assign((istreambuf_iterator<char>(inputFile)),
                        istreambuf_iterator<char>());
    // Close the file
    inputFile.close();

    // create our output file
    ofstream outFile("bf.s");
    output_file = &outFile;

    if (!outFile)
    {
        cout << "could not create output file" << endl;
        return 2; // Return with an error code
    }

    /*
    DONT TOUCH register r13! it's where we keep our current cell address
    */
    asm_setup();

    // Create a function to hold our code, ie main()
    FunctionType *funcType = FunctionType::get(Builder->getInt32Ty(), false); // Return type changed to int (i32)
    mainFunction = Function::Create(funcType, Function::ExternalLinkage, "main", *TheModule);
    BasicBlock *entryBlock = BasicBlock::Create(*TheContext, "entry", mainFunction);

    entryBlockStack.push(entryBlock);

    Builder->SetInsertPoint(entryBlock);

    // Step 1: Create an i8 array of size 10,000
    AllocaInst *arrayAlloc = Builder->CreateAlloca(
        ArrayType::get(Builder->getInt8Ty(), 1000000), nullptr, "myArray");

    // Step 2: Use CreateMemSet to initialize all elements to zero
    Value *zero = Builder->getInt8(0);        // Create a constant zero of type i8
    Value *size = Builder->getInt32(1000000); // Size of the array in i8 elements

    Builder->CreateMemSet(arrayAlloc, zero, size, Align(1), /*isVolatile=*/false);

    // Step 2: Calculate the pointer to the middle of the array
    Value *middleIndex = Builder->getInt32(500000); // Middle of the array index
    middlePtr = Builder->CreateInBoundsGEP(arrayAlloc->getAllocatedType(), arrayAlloc, {zero, middleIndex}, cell_pointer_var);

    if (!optimization_flag)
    {
        // // begin our program compiler loop
        for (int i = 0; i < program_file.size(); i++)
        {
            char ch = program_file[i];
            // cout << "step "<< to_string(i)<<":"<<endl;
            //  TheModule->print(outs(), nullptr);
            //  cout<< endl;

            bf_assembler(ch);
        }
    }

    // Builder->SetInsertPoint(entryBlock);
    //  End the function
    Builder->CreateRet(Builder->getInt32(0)); // Return 0
    // Verify the entire module
    if (verifyModule(*TheModule, &errs()))
    {
        errs() << "Error: Module verification failed.\n";
        TheModule->print(outs(), nullptr);

        return 23;
    }

    // Create the analysis managers.
    // These must be declared in this order so that they are destroyed in the
    // correct order due to inter-analysis-manager references.
    LoopAnalysisManager LAM;
    FunctionAnalysisManager FAM;
    CGSCCAnalysisManager CGAM;
    ModuleAnalysisManager MAM;

    // Create the new pass manager builder.
    // Take a look at the PassBuilder constructor parameters for more
    // customization, e.g. specifying a TargetMachine or various debugging
    // options.
    PassBuilder PB;

    // Register all the basic analyses with the managers.
    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    // Create the pass manager.
    // This one corresponds to a typical -O2 optimization pipeline.
    ModulePassManager MPM = PB.buildPerModuleDefaultPipeline(OptimizationLevel::O0);

    // Optimize the IR!
    MPM.run(*TheModule, MAM);

    // Output the module IR
    TheModule->print(outs(), nullptr);

    // optimize!
    if (optimization_flag)
    {

        vector<string> optimized_program = init_optimized_program_list(program_file);

        // print program without non-instructions
        // print_string_vector(optimized_program);
        unordered_set<int> loop_indices = get_loop_indices(optimized_program);

        // optimize all simple loops and seek loops
        for (auto token : loop_indices)
        {
            vector<string> loop = get_loop_string(token, optimized_program);
            if (is_simple_loop(loop))
            {
                int loop_increment = get_current_cell_change(loop);
                optimized_program = optimize_simple_loop(token, loop_increment, loop, optimized_program);

                // print_string_vector(optimized_program);
            } // end is simple loop

            if (is_seek_loop(loop))
            {
                int seek_offset = is_seek_loop(loop);

                optimized_program = optimize_seek_loop(token, seek_offset, loop, optimized_program);
            } // end is power two

        } // end looping over loop in program list

        // output the assembly
        for (int i = 0; i < optimized_program.size(); i++)
        {
            string token = optimized_program[i];
            bf_string_assembler(token);
        }

        print_string_vector(optimized_program);
    } // end optimized assembler

    asm_cleanup();

    // Close the file
    outFile.close();

} // end main()
