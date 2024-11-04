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
GlobalVariable *globalMiddlePtr;

Function *mainFunction;
// Cursed...
//  Initialize LLVM components
static std::unique_ptr<LLVMContext> TheContext = std::make_unique<LLVMContext>();
static std::unique_ptr<IRBuilder<>> Builder = std::make_unique<IRBuilder<>>(*TheContext);
static std::unique_ptr<Module> TheModule = std::make_unique<Module>("module", *TheContext);

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

    switch (token)
    {

    case '>':
    {
        // Load the current pointer from the global variable
        Value *loadedMiddlePtr = Builder->CreateLoad(Builder->getInt8Ty()->getPointerTo(), globalMiddlePtr, "loadedMiddlePtr");
        // Increment the pointer by 1 byte and save it back to the global variable
        Value *newMiddlePtr = Builder->CreateInBoundsGEP(Builder->getInt8Ty(), loadedMiddlePtr, Builder->getInt32(1), "moveRight");
        Builder->CreateStore(newMiddlePtr, globalMiddlePtr);
    }
    break;

    case '<':
    {
        // Load the current pointer from the global variable
        Value *loadedMiddlePtr = Builder->CreateLoad(Builder->getInt8Ty()->getPointerTo(), globalMiddlePtr, "loadedMiddlePtr");
        // Decrement the pointer by 1 byte and save it back to the global variable
        Value *newMiddlePtr = Builder->CreateInBoundsGEP(Builder->getInt8Ty(), loadedMiddlePtr, Builder->getInt32(-1), "moveLeft");
        Builder->CreateStore(newMiddlePtr, globalMiddlePtr);
    }
    break;

    case '+':
    {
        // Load the value at the current pointer location
        Value *loadedMiddlePtr = Builder->CreateLoad(Builder->getInt8Ty()->getPointerTo(), globalMiddlePtr, "loadedMiddlePtr");
        // Load the value at the pointer
        Value *valueAtPointer = Builder->CreateLoad(Builder->getInt8Ty(), loadedMiddlePtr, "valueAtPointer");

        // Increment the value by 1
        Value *incrementedValue = Builder->CreateAdd(valueAtPointer, Builder->getInt8(1), "incrementedValue");

        // Store the updated value back to the pointer location
        Builder->CreateStore(incrementedValue, loadedMiddlePtr);
    }
    break;

    case '-':
    {
        // Load the value at the current pointer location
        Value *loadedMiddlePtr = Builder->CreateLoad(Builder->getInt8Ty()->getPointerTo(), globalMiddlePtr, "loadedMiddlePtr");
        // Load the value at the pointer
        Value *valueAtPointer = Builder->CreateLoad(Builder->getInt8Ty(), loadedMiddlePtr, "valueAtPointer");

        // Decrement the value by 1
        Value *decrementedValue = Builder->CreateSub(valueAtPointer, Builder->getInt8(1), "decrementedValue");

        // Store the updated value back to the pointer location
        Builder->CreateStore(decrementedValue, loadedMiddlePtr);
    }
    break;

    case '.':
    {
        // Load the value to print
        Value *loadedMiddlePtr = Builder->CreateLoad(Builder->getInt8Ty()->getPointerTo(), globalMiddlePtr, "loadedMiddlePtr");
        Value *valueToPrint = Builder->CreateLoad(Builder->getInt8Ty(), loadedMiddlePtr, "valueToPrint");

        // Create a prototype for putchar (int putchar(int))
        FunctionType *putcharType = FunctionType::get(Builder->getInt32Ty(), Builder->getInt8Ty(), false);
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

        Value *loadedMiddlePtr = Builder->CreateLoad(Builder->getInt8Ty()->getPointerTo(), globalMiddlePtr, "loadedMiddlePtr");
        // Load the value at the pointer

        // Store the character at the location pointed to by middlePtr
        Builder->CreateStore(truncatedInput, loadedMiddlePtr);
    }
    break;

    case '[':
    {

        BasicBlock *loopStart = BasicBlock::Create(*TheContext, "loop_start", mainFunction);
        BasicBlock *afterLoop = BasicBlock::Create(*TheContext, "after_loop", mainFunction);

        loopStartStack.push(loopStart);
        afterLoopStack.push(afterLoop);

        Value *loadedMiddlePtr = Builder->CreateLoad(Builder->getInt8Ty()->getPointerTo(), globalMiddlePtr, "loadedMiddlePtr");
        Value *valueAtPointer = Builder->CreateLoad(Builder->getInt8Ty(), loadedMiddlePtr, "valueAtPointer");
        Value *isZero = Builder->CreateICmpEQ(valueAtPointer, Builder->getInt8(0), "is_zero");
        Builder->CreateCondBr(isZero, afterLoop, loopStart);
        Builder->SetInsertPoint(loopStart);

        break;
    }

    // Handle ']'
    case ']':
    {

        BasicBlock *loopStart = loopStartStack.top();
        BasicBlock *afterLoop = afterLoopStack.top();

        loopStartStack.pop();
        afterLoopStack.pop();

        Value *loadedMiddlePtr = Builder->CreateLoad(Builder->getInt8Ty()->getPointerTo(), globalMiddlePtr, "loadedMiddlePtr");
        Value *valueAtPointer = Builder->CreateLoad(Builder->getInt8Ty(), loadedMiddlePtr, "valueAtPointer");
        Value *isZero = Builder->CreateICmpEQ(valueAtPointer, Builder->getInt8(0), "is_zero");
        Builder->CreateCondBr(isZero, afterLoop, loopStart);
        Builder->SetInsertPoint(afterLoop);

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

    //////
    if (token == ">")
    { // Load the current pointer from the global variable
        Value *loadedMiddlePtr = Builder->CreateLoad(Builder->getInt8Ty()->getPointerTo(), globalMiddlePtr, "loadedMiddlePtr");
        // Increment the pointer by 1 byte and save it back to the global variable
        Value *newMiddlePtr = Builder->CreateInBoundsGEP(Builder->getInt8Ty(), loadedMiddlePtr, Builder->getInt32(1), "moveRight");
        Builder->CreateStore(newMiddlePtr, globalMiddlePtr);
    }
    if (token == "<")
    {
        // Load the current pointer from the global variable
        Value *loadedMiddlePtr = Builder->CreateLoad(Builder->getInt8Ty()->getPointerTo(), globalMiddlePtr, "loadedMiddlePtr");
        // Decrement the pointer by 1 byte and save it back to the global variable
        Value *newMiddlePtr = Builder->CreateInBoundsGEP(Builder->getInt8Ty(), loadedMiddlePtr, Builder->getInt32(-1), "moveLeft");
        Builder->CreateStore(newMiddlePtr, globalMiddlePtr);
    }
    if (token == "+")
    {
        // Load the value at the current pointer location
        Value *loadedMiddlePtr = Builder->CreateLoad(Builder->getInt8Ty()->getPointerTo(), globalMiddlePtr, "loadedMiddlePtr");
        // Load the value at the pointer
        Value *valueAtPointer = Builder->CreateLoad(Builder->getInt8Ty(), loadedMiddlePtr, "valueAtPointer");

        // Increment the value by 1
        Value *incrementedValue = Builder->CreateAdd(valueAtPointer, Builder->getInt8(1), "incrementedValue");

        // Store the updated value back to the pointer location
        Builder->CreateStore(incrementedValue, loadedMiddlePtr);
    }

    if (token == "-")
    {
        // Load the value at the current pointer location
        Value *loadedMiddlePtr = Builder->CreateLoad(Builder->getInt8Ty()->getPointerTo(), globalMiddlePtr, "loadedMiddlePtr");
        // Load the value at the pointer
        Value *valueAtPointer = Builder->CreateLoad(Builder->getInt8Ty(), loadedMiddlePtr, "valueAtPointer");

        // Decrement the value by 1
        Value *decrementedValue = Builder->CreateSub(valueAtPointer, Builder->getInt8(1), "decrementedValue");

        // Store the updated value back to the pointer location
        Builder->CreateStore(decrementedValue, loadedMiddlePtr);
    }

    if (token == ".")
    {
        // Load the value to print
        Value *loadedMiddlePtr = Builder->CreateLoad(Builder->getInt8Ty()->getPointerTo(), globalMiddlePtr, "loadedMiddlePtr");
        Value *valueToPrint = Builder->CreateLoad(Builder->getInt8Ty(), loadedMiddlePtr, "valueToPrint");

        // Create a prototype for putchar (int putchar(int))
        FunctionType *putcharType = FunctionType::get(Builder->getInt32Ty(), Builder->getInt8Ty(), false);
        FunctionCallee putcharFunction = TheModule->getOrInsertFunction("putchar", putcharType);

        // Call putchar with the loaded value
        Builder->CreateCall(putcharFunction, valueToPrint);
    }
    if (token == ",")
    {
        // Create a prototype for getchar (int getchar())
        FunctionType *getcharType = FunctionType::get(Type::getInt32Ty(*TheContext), false);
        FunctionCallee getcharFunction = TheModule->getOrInsertFunction("getchar", getcharType);

        // Call getchar to read a character from stdin
        // Call getchar to read a character from stdin
        Value *inputChar = Builder->CreateCall(getcharFunction, {}, "inputChar");

        // Truncate the returned int32 to int8 if needed (for 8-bit storage)
        Value *truncatedInput = Builder->CreateTrunc(inputChar, Type::getInt8Ty(*TheContext), "truncatedInput");

        Value *loadedMiddlePtr = Builder->CreateLoad(Builder->getInt8Ty()->getPointerTo(), globalMiddlePtr, "loadedMiddlePtr");
        // Load the value at the pointer

        // Store the character at the location pointed to by middlePtr
        Builder->CreateStore(truncatedInput, loadedMiddlePtr);
    }
    if (token == "[")
    {

        BasicBlock *loopStart = BasicBlock::Create(*TheContext, "loop_start", mainFunction);
        BasicBlock *afterLoop = BasicBlock::Create(*TheContext, "after_loop", mainFunction);

        loopStartStack.push(loopStart);
        afterLoopStack.push(afterLoop);

        Value *loadedMiddlePtr = Builder->CreateLoad(Builder->getInt8Ty()->getPointerTo(), globalMiddlePtr, "loadedMiddlePtr");
        Value *valueAtPointer = Builder->CreateLoad(Builder->getInt8Ty(), loadedMiddlePtr, "valueAtPointer");
        Value *isZero = Builder->CreateICmpEQ(valueAtPointer, Builder->getInt8(0), "is_zero");
        Builder->CreateCondBr(isZero, afterLoop, loopStart);
        Builder->SetInsertPoint(loopStart);
    }
    if (token == "]")
    {

        BasicBlock *loopStart = loopStartStack.top();
        BasicBlock *afterLoop = afterLoopStack.top();

        loopStartStack.pop();
        afterLoopStack.pop();

        Value *loadedMiddlePtr = Builder->CreateLoad(Builder->getInt8Ty()->getPointerTo(), globalMiddlePtr, "loadedMiddlePtr");
        Value *valueAtPointer = Builder->CreateLoad(Builder->getInt8Ty(), loadedMiddlePtr, "valueAtPointer");
        Value *isZero = Builder->CreateICmpEQ(valueAtPointer, Builder->getInt8(0), "is_zero");
        Builder->CreateCondBr(isZero, afterLoop, loopStart);
        Builder->SetInsertPoint(afterLoop);
    }

    if (startsWith(token, "expr_simple:"))
    {
        // cout<< token<<endl;

        // main()

        //        expr_simple:--1:1,0:-1,

        string sign_of_loop;

        // if +, we perform loop 256-255 times
        // if -, we perform full p[0] times
        if (startsWith(token, "expr_simple:+"))
            sign_of_loop = "+";

        if (startsWith(token, "expr_simple:-"))
            sign_of_loop = "-";

        //////////////////////////////////

        map<int, int> simple_expr = expr_string_to_dict(token);

        // Load the base pointer from the global variable only once
        Value *loadedMiddlePtr = Builder->CreateLoad(Builder->getInt8Ty()->getPointerTo(), globalMiddlePtr, "loadedMiddlePtr");

        // Load the value at the current pointer location
        Value *p0 = Builder->CreateLoad(Builder->getInt8Ty(), loadedMiddlePtr, "valueAtPointer");

        // if (sign_of_loop == "-")
        //     do nothing

        if (sign_of_loop == "+")
        {
            // input can't be more than 255 so we're good

            // Subtract the loaded value (p0) from 256
            Value *adjustedValue = Builder->CreateSub(Builder->getInt8(256), p0, "adjustedValue");
            // adjustedValue = Builder->CreateSub(Builder->getInt8(1), p0, "adjustedValue");

            // Update p0 with the new adjusted value
            p0 = adjustedValue;
        }

        for (const auto &pair : simple_expr)
        {
            // if (pair.first == 0)
            //     continue;

            // Calculate the new pointer by offsetting the loaded pointer
            Value *newMiddlePtr = Builder->CreateInBoundsGEP(Builder->getInt8Ty(), loadedMiddlePtr, Builder->getInt32(pair.first), "offsetPointer");

            // Load the current value at the new pointer location
            Value *currentValue = Builder->CreateLoad(Builder->getInt8Ty(), newMiddlePtr, "currentValue");

            // Create a constant for pair.second (assuming it's an integer, adjust the type if needed)
            Value *changeValue = Builder->getInt8(pair.second);

            // Multiply the base value (p0) by the change amount
            Value *multipliedValue = Builder->CreateMul(p0, changeValue, "multipliedValue");

            // Add the multiplied value to the current value at newMiddlePtr
            Value *newValue = Builder->CreateAdd(currentValue, multipliedValue, "newValue");

            // Store the result back at the new pointer location
            Builder->CreateStore(newValue, newMiddlePtr);
        }

        // our loop should always end in 0, this assures it, but would break
        // intentional infinite loops ¯\_(ツ)_/¯, saves us like 5 instr per loop

        // Load the value at the current pointer location
        // Value *loadedMiddlePtr2 = Builder->CreateLoad(Builder->getInt8Ty()->getPointerTo(), globalMiddlePtr, "loadedMiddlePtr");
        // Load the value at the pointer
        // Store the updated value back to the pointer location
        Builder->CreateStore(Builder->getInt8(0), loadedMiddlePtr);

        print_padding();
    }

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
    ofstream outFile("bf.ll");
    output_file = &outFile;

    if (!outFile)
    {
        cout << "could not create output file" << endl;
        return 2; // Return with an error code
    }

    // Create a function to hold our code, ie main()
    FunctionType *funcType = FunctionType::get(Builder->getInt32Ty(), false); // Return type changed to int (i32)
    mainFunction = Function::Create(funcType, Function::ExternalLinkage, "main", *TheModule);
    BasicBlock *entryBlock = BasicBlock::Create(*TheContext, "entry", mainFunction);
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

    // Step 5: Create a global variable to hold the pointer to the middle of the array
    globalMiddlePtr = new GlobalVariable(
        *TheModule,                                                     // Module
        Builder->getInt8Ty()->getPointerTo(),                           // Type: pointer to i8
        false,                                                          // isConstant
        GlobalValue::ExternalLinkage,                                   // Linkage
        ConstantPointerNull::get(Builder->getInt8Ty()->getPointerTo()), // Initialize to null
        "globalMiddlePtr"                                               // Name
    );

    // Step 6: Store the pointer into the global variable
    Builder->CreateStore(middlePtr, globalMiddlePtr);

    // Value *loadedMiddlePtr = Builder->CreateLoad(Builder->getInt8Ty()->getPointerTo(), globalMiddlePtr, "loadedMiddlePtr");

    if (0)
    {
        // // begin our program compiler loop
        for (int i = 0; i < program_file.size(); i++)
        {
            char ch = program_file[i];
            bf_assembler(ch);
        }
    }

    // optimize!
    if (1)
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

                //  optimized_program = optimize_seek_loop(token, seek_offset, loop, optimized_program);
            } // end is power two

        } // end looping over loop in program list

        // output the assembly
        for (int i = 0; i < optimized_program.size(); i++)
        {
            string token = optimized_program[i];
            bf_string_assembler(token);
        }

        // print_string_vector(optimized_program);
    } // end optimized assembler

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
    ModulePassManager MPM = PB.buildPerModuleDefaultPipeline(OptimizationLevel::O2);

    // Optimize the IR!
    MPM.run(*TheModule, MAM);

    // Output the module IR
    // TheModule->print(outs(), nullptr);

    std::string irString;
    llvm::raw_string_ostream irStream(irString);
    TheModule->print(irStream, nullptr); // This will print the IR to `irStream`, filling `irString`.

    // Step 2: Use the captured IR string as needed
    jasm(irString.c_str()); // Assuming jasm accepts a C-string

    // Close the file
    outFile.close();

} // end main()
