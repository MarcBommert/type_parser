
Author: Marc Bommert - 2018/08/18

type hierarchy parser and c# backend
------------------------------------

Based on libclang, we parse a given header/source file with its full inclusion hierarchy. 
Then, we parse the tokenized source for preprocessor defines and afterwards 
parse the compilation unit for typedef-hierarchies,
write them into a binary representation,
with the goal of letting the backend emit C# constants (for defines) and marshallable c# structure definitions (for typedef-hierachies)

type_parser is awful c++
backend is in awful C#
Both are Visual Studio 2017 projects

Developed with LLVM 3.9.0
Create because I required it for a specific task and also I was interested in LLVM.
This is not systematically tested, nor is the code clean. Published for demonstrational purpose.
Use at your own risk.
