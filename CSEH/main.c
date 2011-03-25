#include <stdio.h>
#include <stdlib.h>
#include "seh.h"

void generate_divide_by_zero()
{
	int num=1,divider=0;

	printf("divide by zero SEH test begin\n");

	SEH_TRY(1)
	{
		printf("dividing...\n");
		num /= divider;
	}
	SEH_CATCH(1)
	{
		printf("an exception occurred\n");
	}
	SEH_END(1);

	printf("divide by zero SEH test end\n\n");
}

void generate_segfault1()
{
	char x[5];
	int i=5;

	printf("segmentation fault SEH test#1 begin\n");

	SEH_TRY(1)
	{
		printf("filling buffer\n");

		while (i++)
			x[i] = 0;
	}
	SEH_CATCH(1)
	{
		printf("an exception occurred\n");
		printf("fault when x[%d] = 0\n",i);
		printf("this program will exit. because buffer overflowed. press enter for exit\n");
		fflush(stdin);
		getc(stdin);
		exit(1);
	}
	SEH_END(1);

	printf("segmentation fault SEH test#1 end\n\n");
}


void generate_segfault2()
{
	char *badptr = (char *)0xdeadbeef;
	char c;

	printf("segmentation fault SEH test#2 begin\n");

	SEH_TRY(1)
	{
		printf("accessing to a badptr\n");
		c = *badptr;
		*badptr = c;
	}
	SEH_CATCH(1)
	{
		printf("an exception occurred\n");
	}
	SEH_END(1);

	printf("segmentation fault SEH test#2 end\n\n");
}

void generate_fault_with_catchex(unsigned char is_segfault)
{
	unsigned long exception=0;
	char *p=NULL;
	int num=10,div=0;

	printf("fault test with SEH_CATCH_EX begin\n");

	SEH_TRY(1)
	{
		if (is_segfault)
		{
			printf("it will cause a seg fault\n");
			*p = 100;
		}
		else
		{
			printf("it will cause divide by zero\n");
			num /= div;
		}
	}
	SEH_CATCH_EX(1,&exception)
	{
		switch (exception)
		{
		case CSEH_SEGMENTATION_FAULT:
			printf("Segmentation Fault\n");
			break;
		case CSEH_DIVIDE_BY_ZERO:
			printf("Divide by zero\n");
			break;
		case CSEH_FPE_FAULT:
			printf("Floating point fault\n");
		}
	}
	SEH_END(1);

	printf("fault test with SEH_CATCH_EX end\n\n");
}

int main()
{
	generate_segfault2();
	generate_fault_with_catchex(1);
	generate_divide_by_zero();
	generate_fault_with_catchex(0);
	generate_segfault1();
	return 0;
}
