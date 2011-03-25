Structured Exception Handler Project for C

Short Description:

Normally, C doesn't support any SEH mechanism. This project aims to support structured exception handling. It's running on Windows and Linux. Even you can use the SEH into multi-threaded applications. (But not recomended at this time). This project uses posix signal handling mechanism for both OSes. And also uses Vectored Exception Handling (VEH) for Windows OS. Its still under development. For this reason, you may catch some bugs in the code and you can open an issue topic on github if you want. 

Vectored Exception Handling enabled by default. To disable use "#undef USE_VEH" 
P.S. When you disable VEH, you cannot handle the "divide by zero" exceptions. Because Windows POSIX signal implementation doesn't support this.

Syntax

It has two usage type.

$SehIdentifier: An integer ID. It will use to internal SEH mechanism and multithreaded SEH.

First,

SEH_TRY($SehIdentifier)
{
	WORK STUFF
}
SEH_CATCH($SehIdentifier)
{
	EXCEPTION HANDLING BLOCK.
}
SEH_END($SehIdentifier);

And other type 

long ExceptionCodeVariable=0;

SEH_TRY($SehIdentifier)
{
}
SEH_CATCH_EX($SehIdentifier,&ExceptionCodeVariable)
{
	switch(ExceptionCodeVariable)
	{
		case ...
	}
}
SEH_END($SehIdentifier);

SEH_CATCH_EX gives to you occurred exception type. And it can be following values.

CSEH_SEGMENTATION_FAULT : Bad memory reference or invalid memory access
CSEH_FPE_FAULT : Floating point exception.
CSEH_ILLEGAL_INSTRUCTION : Illegal instruction
CSEH_DIVIDE_BY_ZERO : Divider is zero.
CSEH_NONCOUNTABLE_FAULT : Non countable exception. 

Examples:

main.c contains a few example codes.


