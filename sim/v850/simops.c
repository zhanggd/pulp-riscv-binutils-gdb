#include <signal.h>
#include "v850_sim.h"
#include "simops.h"
 /* FIXME - should be including a version of syscall.h that does not
    pollute the name space */
#include "../../libgloss/v850/sys/syscall.h"
#include "bfd.h"
#include <errno.h>
#if !defined(__GO32__) && !defined(_WIN32)
#include <sys/stat.h>
#include <sys/times.h>
#include <sys/time.h>
#endif

enum op_types
{
  OP_UNKNOWN,
  OP_NONE,
  OP_TRAP,
  OP_REG,
  OP_REG_REG,
  OP_REG_REG_CMP,
  OP_REG_REG_MOVE,
  OP_IMM_REG,
  OP_IMM_REG_CMP,
  OP_IMM_REG_MOVE,
  OP_COND_BR,
  OP_LOAD16,
  OP_STORE16,
  OP_LOAD32,
  OP_STORE32,
  OP_JUMP,
  OP_IMM_REG_REG,
  OP_UIMM_REG_REG,
  OP_BIT,
  OP_EX1,
  OP_EX2,
  OP_LDSR,
  OP_STSR,
/* start-sanitize-v850e */
  OP_BIT_CHANGE,
  OP_REG_REG_REG,
  OP_REG_REG3,
/* end-sanitize-v850e */
/* start-sanitize-v850eq */
  OP_IMM_REG_REG_REG,
  OP_PUSHPOP1,
  OP_PUSHPOP2,
  OP_PUSHPOP3,
/* end-sanitize-v850eq */
};

/* start-sanitize-v850e */
/* This is an array of the bit positions of registers r20 .. r31 in that order in a prepare/dispose instruction.  */
static int type1_regs[12] = { 27, 26, 25, 24, 31, 30, 29, 28, 23, 22, 0, 21 };
/* end-sanitize-v850e */
/* start-sanitize-v850eq */
/* This is an array of the bit positions of registers r16 .. r31 in that order in a push/pop instruction.  */
static int type2_regs[16] = { 3, 2, 1, 0, 27, 26, 25, 24, 31, 30, 29, 28, 23, 22, 20, 21};
/* This is an array of the bit positions of registers r1 .. r15 in that order in a push/pop instruction.  */
static int type3_regs[15] = { 2, 1, 0, 27, 26, 25, 24, 31, 30, 29, 28, 23, 22, 20, 21};
/* end-sanitize-v850eq */

#ifdef DEBUG
static void trace_input PARAMS ((char *name, enum op_types type, int size));
static void trace_output PARAMS ((enum op_types result));
static int init_text_p = 0;
static asection *text;
static bfd_vma text_start;
static bfd_vma text_end;
extern bfd *prog_bfd;

#ifndef SIZE_INSTRUCTION
#define SIZE_INSTRUCTION 6
#endif

#ifndef SIZE_OPERANDS
#define SIZE_OPERANDS 16
#endif

#ifndef SIZE_VALUES
#define SIZE_VALUES 11
#endif

#ifndef SIZE_LOCATION
#define SIZE_LOCATION 40
#endif


static void
trace_input (name, type, size)
     char *name;
     enum op_types type;
     int size;
{
  char buf[1024];
  char *p;
  uint32 values[3];
  int num_values, i;
  char *cond;
  asection *s;
  const char *filename;
  const char *functionname;
  unsigned int linenumber;

  if ((v850_debug & DEBUG_TRACE) == 0)
    return;

  buf[0] = '\0';
  if (!init_text_p)
    {
      init_text_p = 1;
      for (s = prog_bfd->sections; s; s = s->next)
	if (strcmp (bfd_get_section_name (prog_bfd, s), ".text") == 0)
	  {
	    text = s;
	    text_start = bfd_get_section_vma (prog_bfd, s);
	    text_end = text_start + bfd_section_size (prog_bfd, s);
	    break;
	  }
    }

  if (text && PC >= text_start && PC < text_end)
    {
      filename = (const char *)0;
      functionname = (const char *)0;
      linenumber = 0;
      if (bfd_find_nearest_line (prog_bfd, text, (struct symbol_cache_entry **)0, PC - text_start,
				 &filename, &functionname, &linenumber))
	{
	  p = buf;
	  if (linenumber)
	    {
	      sprintf (p, "Line %5d ", linenumber);
	      p += strlen (p);
	    }

	  if (functionname)
	    {
	      sprintf (p, "Func %s ", functionname);
	      p += strlen (p);
	    }
	  else if (filename)
	    {
	      char *q = (char *) strrchr (filename, '/');
	      sprintf (p, "File %s ", (q) ? q+1 : filename);
	      p += strlen (p);
	    }

	  if (*p == ' ')
	    *p = '\0';
	}
    }

  (*v850_callback->printf_filtered) (v850_callback, "0x%.8x: %-*.*s %-*s",
				     (unsigned)PC,
				     SIZE_LOCATION, SIZE_LOCATION, buf,
				     SIZE_INSTRUCTION, name);

  switch (type)
    {
    default:
    case OP_UNKNOWN:
    case OP_NONE:
      strcpy (buf, "unknown");
      break;

    case OP_TRAP:
      sprintf (buf, "%d", OP[0]);
      break;

    case OP_REG:
      sprintf (buf, "r%d", OP[0]);
      break;

    case OP_REG_REG:
    case OP_REG_REG_CMP:
    case OP_REG_REG_MOVE:
      sprintf (buf, "r%d,r%d", OP[0], OP[1]);
      break;

    case OP_IMM_REG:
    case OP_IMM_REG_CMP:
    case OP_IMM_REG_MOVE:
      sprintf (buf, "%d,r%d", OP[0], OP[1]);
      break;

    case OP_COND_BR:
      sprintf (buf, "%d", SEXT9 (OP[0]));
      break;

    case OP_LOAD16:
      sprintf (buf, "%d[r30],r%d", OP[1] * size, OP[0]);
      break;

    case OP_STORE16:
      sprintf (buf, "r%d,%d[r30]", OP[0], OP[1] * size);
      break;

    case OP_LOAD32:
      sprintf (buf, "%d[r%d],r%d", SEXT16 (OP[2]) & ~0x1, OP[0], OP[1]);
      break;

    case OP_STORE32:
      sprintf (buf, "r%d,%d[r%d]", OP[1], SEXT16 (OP[2] & ~0x1), OP[0]);
      break;

    case OP_JUMP:
      sprintf (buf, "%d,r%d", SEXT22 (OP[0]), OP[1]);
      break;

    case OP_IMM_REG_REG:
      sprintf (buf, "%d,r%d,r%d", SEXT16 (OP[0]), OP[1], OP[2]);
      break;

    case OP_UIMM_REG_REG:
      sprintf (buf, "%d,r%d,r%d", OP[0] & 0xffff, OP[1], OP[2]);
      break;

    case OP_BIT:
      sprintf (buf, "%d,%d[r%d]", OP[1] & 0x7, SEXT16 (OP[2]), OP[0]);
      break;

    case OP_EX1:
      switch (OP[0] & 0xf)
	{
	default:  cond = "?";	break;
	case 0x0: cond = "v";	break;
	case 0x1: cond = "c";	break;
	case 0x2: cond = "z";	break;
	case 0x3: cond = "nh";	break;
	case 0x4: cond = "s";	break;
	case 0x5: cond = "t";	break;
	case 0x6: cond = "lt";	break;
	case 0x7: cond = "le";	break;
	case 0x8: cond = "nv";	break;
	case 0x9: cond = "nc";	break;
	case 0xa: cond = "nz";	break;
	case 0xb: cond = "h";	break;
	case 0xc: cond = "ns";	break;
	case 0xd: cond = "sa";	break;
	case 0xe: cond = "ge";	break;
	case 0xf: cond = "gt";	break;
	}

      sprintf (buf, "%s,r%d", cond, OP[1]);
      break;

    case OP_EX2:
      strcpy (buf, "EX2");
      break;

    case OP_LDSR:
    case OP_STSR:
      sprintf (buf, "r%d,s%d", OP[0], OP[1]);
      break;

    case OP_PUSHPOP1:
      for (i = 0; i < 12; i++)
	if (OP[3] & (1 << type1_regs[i]))
	  strcat (buf, "r%d ", i + 20);
      break;

    case OP_PUSHPOP2:
      for (i = 0; i < 16; i++)
	if (OP[3] & (1 << type2_regs[i]))
	  strcat (buf, "r%d ", i + 16);
      if (OP[3] & (1 << 19))
	strcat (buf, "F/EIPC, F/EIPSW " );
      break;

    case OP_PUSHPOP3:
      for (i = 0; i < 15; i++)
	if (OP[3] & (1 << type3_regs[i]))
	  strcat (buf, "r%d ", i + 1);
      if (OP[3] & (1 << 3))
	strcat (buf, "PSW " );
      if (OP[3] & (1 << 19))
	strcat (buf, "F/EIPC, F/EIPSW " );
      break;

    case OP_BIT_CHANGE:
      sprintf (buf, "r%d, [r%d]", OP[1], OP[0] );
      break;
    }

  if ((v850_debug & DEBUG_VALUES) == 0)
    {
      (*v850_callback->printf_filtered) (v850_callback, "%s\n", buf);
    }
  else
    {
      (*v850_callback->printf_filtered) (v850_callback, "%-*s", SIZE_OPERANDS, buf);
      switch (type)
	{
	default:
	case OP_UNKNOWN:
	case OP_NONE:
	case OP_TRAP:
	  num_values = 0;
	  break;

	case OP_REG:
	case OP_REG_REG_MOVE:
	  values[0] = State.regs[OP[0]];
	  num_values = 1;
	  break;

	case OP_BIT_CHANGE:
	case OP_REG_REG:
	case OP_REG_REG_CMP:
	  values[0] = State.regs[OP[1]];
	  values[1] = State.regs[OP[0]];
	  num_values = 2;
	  break;

	case OP_IMM_REG:
	case OP_IMM_REG_CMP:
	  values[0] = SEXT5 (OP[0]);
	  values[1] = OP[1];
	  num_values = 2;
	  break;

	case OP_IMM_REG_MOVE:
	  values[0] = SEXT5 (OP[0]);
	  num_values = 1;
	  break;

	case OP_COND_BR:
	  values[0] = State.pc;
	  values[1] = SEXT9 (OP[0]);
	  values[2] = PSW;
	  num_values = 3;
	  break;

	case OP_LOAD16:
	  values[0] = OP[1] * size;
	  values[1] = State.regs[30];
	  num_values = 2;
	  break;

	case OP_STORE16:
	  values[0] = State.regs[OP[0]];
	  values[1] = OP[1] * size;
	  values[2] = State.regs[30];
	  num_values = 3;
	  break;

	case OP_LOAD32:
	  values[0] = SEXT16 (OP[2]);
	  values[1] = State.regs[OP[0]];
	  num_values = 2;
	  break;

	case OP_STORE32:
	  values[0] = State.regs[OP[1]];
	  values[1] = SEXT16 (OP[2]);
	  values[2] = State.regs[OP[0]];
	  num_values = 3;
	  break;

	case OP_JUMP:
	  values[0] = SEXT22 (OP[0]);
	  values[1] = State.pc;
	  num_values = 2;
	  break;

	case OP_IMM_REG_REG:
	  values[0] = SEXT16 (OP[0]) << size;
	  values[1] = State.regs[OP[1]];
	  num_values = 2;
	  break;

	case OP_UIMM_REG_REG:
	  values[0] = (OP[0] & 0xffff) << size;
	  values[1] = State.regs[OP[1]];
	  num_values = 2;
	  break;

	case OP_BIT:
	  num_values = 0;
	  break;

	case OP_EX1:
	  values[0] = PSW;
	  num_values = 1;
	  break;

	case OP_EX2:
	  num_values = 0;
	  break;

	case OP_LDSR:
	  values[0] = State.regs[OP[0]];
	  num_values = 1;
	  break;

	case OP_STSR:
	  values[0] = State.sregs[OP[1]];
	  num_values = 1;
	}

      for (i = 0; i < num_values; i++)
	(*v850_callback->printf_filtered) (v850_callback, "%*s0x%.8lx", SIZE_VALUES - 10, "", values[i]);

      while (i++ < 3)
	(*v850_callback->printf_filtered) (v850_callback, "%*s", SIZE_VALUES, "");
    }
}

static void
trace_output (result)
     enum op_types result;
{
  if ((v850_debug & (DEBUG_TRACE | DEBUG_VALUES)) == (DEBUG_TRACE | DEBUG_VALUES))
    {
      switch (result)
	{
	default:
	case OP_UNKNOWN:
	case OP_NONE:
	case OP_TRAP:
	case OP_REG:
	case OP_REG_REG_CMP:
	case OP_IMM_REG_CMP:
	case OP_COND_BR:
	case OP_STORE16:
	case OP_STORE32:
	case OP_BIT:
	case OP_EX2:
	  break;

	case OP_LOAD16:
	case OP_STSR:
	  (*v850_callback->printf_filtered) (v850_callback, " :: 0x%.8lx",
					     (unsigned long)State.regs[OP[0]]);
	  break;

	case OP_REG_REG:
	case OP_REG_REG_MOVE:
	case OP_IMM_REG:
	case OP_IMM_REG_MOVE:
	case OP_LOAD32:
	case OP_EX1:
	  (*v850_callback->printf_filtered) (v850_callback, " :: 0x%.8lx",
					     (unsigned long)State.regs[OP[1]]);
	  break;

	case OP_IMM_REG_REG:
	case OP_UIMM_REG_REG:
	  (*v850_callback->printf_filtered) (v850_callback, " :: 0x%.8lx",
					     (unsigned long)State.regs[OP[2]]);
	  break;

	case OP_JUMP:
	  if (OP[1] != 0)
	    (*v850_callback->printf_filtered) (v850_callback, " :: 0x%.8lx",
					       (unsigned long)State.regs[OP[1]]);
	  break;

	case OP_LDSR:
	  (*v850_callback->printf_filtered) (v850_callback, " :: 0x%.8lx",
					     (unsigned long)State.sregs[OP[1]]);
	  break;
	}

      (*v850_callback->printf_filtered) (v850_callback, "\n");
    }
}

#else
#define trace_input(NAME, IN1, IN2)
#define trace_output(RESULT)

//#define trace_input(NAME, IN1, IN2) fprintf (stderr, NAME "\n" );

#endif


/* Returns 1 if the specific condition is met, returns 0 otherwise.  */
static unsigned int
condition_met (unsigned code)
{
  unsigned int psw = PSW;

  switch (code & 0xf)
    {
      case 0x0: return ((psw & PSW_OV) != 0); 
      case 0x1:	return ((psw & PSW_CY) != 0);
      case 0x2:	return ((psw & PSW_Z) != 0);
      case 0x3:	return ((((psw & PSW_CY) != 0) | ((psw & PSW_Z) != 0)) != 0);
      case 0x4:	return ((psw & PSW_S) != 0);
    /*case 0x5:	return 1;*/
      case 0x6: return ((((psw & PSW_S) != 0) ^ ((psw & PSW_OV) != 0)) != 0);
      case 0x7:	return (((((psw & PSW_S) != 0) ^ ((psw & PSW_OV) != 0)) || ((psw & PSW_Z) != 0)) != 0);
      case 0x8:	return ((psw & PSW_OV) == 0);
      case 0x9:	return ((psw & PSW_CY) == 0);
      case 0xa:	return ((psw & PSW_Z) == 0);
      case 0xb:	return ((((psw & PSW_CY) != 0) | ((psw & PSW_Z) != 0)) == 0);
      case 0xc:	return ((psw & PSW_S) == 0);
      case 0xd:	return ((psw & PSW_SAT) != 0);
      case 0xe:	return ((((psw & PSW_S) != 0) ^ ((psw & PSW_OV) != 0)) == 0);
      case 0xf:	return (((((psw & PSW_S) != 0) ^ ((psw & PSW_OV) != 0)) || ((psw & PSW_Z) != 0)) == 0);
    }
  
  return 1;
}

static unsigned long
Add32 (unsigned long a1, unsigned long a2, int * carry)
{
  unsigned long result = (a1 + a2);

  * carry = (result < a1);

  return result;
}

static void
Multiply64 (boolean sign, unsigned long op0)
{
  unsigned long op1;
  unsigned long lo;
  unsigned long mid1;
  unsigned long mid2;
  unsigned long hi;
  unsigned long RdLo;
  unsigned long RdHi;
  int           carry;
  
  op1 = State.regs[ OP[1] ];

  if (sign)
    {
      /* Compute sign of result and adjust operands if necessary.  */
	  
      sign = (op0 ^ op1) & 0x80000000;
	  
      if (((signed long) op0) < 0)
	op0 = - op0;
	  
      if (((signed long) op1) < 0)
	op1 = - op1;
    }
      
  /* We can split the 32x32 into four 16x16 operations. This ensures
     that we do not lose precision on 32bit only hosts: */
  lo   = ( (op0        & 0xFFFF) *  (op1        & 0xFFFF));
  mid1 = ( (op0        & 0xFFFF) * ((op1 >> 16) & 0xFFFF));
  mid2 = (((op0 >> 16) & 0xFFFF) *  (op1        & 0xFFFF));
  hi   = (((op0 >> 16) & 0xFFFF) * ((op1 >> 16) & 0xFFFF));
  
  /* We now need to add all of these results together, taking care
     to propogate the carries from the additions: */
  RdLo = Add32 (lo, (mid1 << 16), & carry);
  RdHi = carry;
  RdLo = Add32 (RdLo, (mid2 << 16), & carry);
  RdHi += (carry + ((mid1 >> 16) & 0xFFFF) + ((mid2 >> 16) & 0xFFFF) + hi);

  if (sign)
    {
      /* Negate result if necessary.  */
      
      RdLo = ~ RdLo;
      RdHi = ~ RdHi;
      if (RdLo == 0xFFFFFFFF)
	{
	  RdLo = 0;
	  RdHi += 1;
	}
      else
	RdLo += 1;
    }
  
  State.regs[ OP[1]       ] = RdLo;
  State.regs[ OP[2] >> 11 ] = RdHi;

  return;
}


/* sld.b */
int
OP_300 ()
{
  unsigned long result;
  
  result = load_mem (State.regs[30] + (OP[3] & 0x7f), 1);

/* start-sanitize-v850eq */
#ifdef ARCH_v850eq
  trace_input ("sld.bu", OP_LOAD16, 1);
  
  State.regs[ OP[1] ] = result;
#else
/* end-sanitize-v850eq */
  trace_input ("sld.b", OP_LOAD16, 1);
  
  State.regs[ OP[1] ] = SEXT8 (result);
/* start-sanitize-v850eq */
#endif
/* end-sanitize-v850eq */
  
  trace_output (OP_LOAD16);
  
  return 2;
}

/* sld.h */
int
OP_400 ()
{
  unsigned long result;
  
  result = load_mem (State.regs[30] + ((OP[3] & 0x7f) << 1), 2);

/* start-sanitize-v850eq */
#ifdef ARCH_v850eq
  trace_input ("sld.hu", OP_LOAD16, 2);
  
  State.regs[ OP[1] ] = result;
#else
/* end-sanitize-v850eq */
  trace_input ("sld.h", OP_LOAD16, 2);
  
  State.regs[ OP[1] ] = SEXT16 (result);
/* start-sanitize-v850eq */
#endif
/* end-sanitize-v850eq */
  
  trace_output (OP_LOAD16);

  return 2;
}

/* sld.w */
int
OP_500 ()
{
  trace_input ("sld.w", OP_LOAD16, 4);
  
  State.regs[ OP[1] ] = load_mem (State.regs[30] + ((OP[3] & 0x7f) << 1), 4);
  
  trace_output (OP_LOAD16);
  
  return 2;
}

/* sst.b */
int
OP_380 ()
{
  trace_input ("sst.b", OP_STORE16, 1);

  store_mem (State.regs[30] + (OP[3] & 0x7f), 1, State.regs[ OP[1] ]);
  
  trace_output (OP_STORE16);

  return 2;
}

/* sst.h */
int
OP_480 ()
{
  trace_input ("sst.h", OP_STORE16, 2);

  store_mem (State.regs[30] + ((OP[3] & 0x7f) << 1), 2, State.regs[ OP[1] ]);
  
  trace_output (OP_STORE16);

  return 2;
}

/* sst.w */
int
OP_501 ()
{
  trace_input ("sst.w", OP_STORE16, 4);

  store_mem (State.regs[30] + ((OP[3] & 0x7e) << 1), 4, State.regs[ OP[1] ]);
  
  trace_output (OP_STORE16);

  return 2;
}

/* ld.b */
int
OP_700 ()
{
  int adr;

  trace_input ("ld.b", OP_LOAD32, 1);

  adr = State.regs[ OP[0] ] + SEXT16 (OP[2]);

  State.regs[ OP[1] ] = SEXT8 (load_mem (adr, 1));
  
  trace_output (OP_LOAD32);

  return 4;
}

/* ld.h */
int
OP_720 ()
{
  int adr;

  trace_input ("ld.h", OP_LOAD32, 2);

  adr = State.regs[ OP[0] ] + SEXT16 (OP[2]);
  adr &= ~0x1;
  
  State.regs[ OP[1] ] = SEXT16 (load_mem (adr, 2));
  
  trace_output (OP_LOAD32);

  return 4;
}

/* ld.w */
int
OP_10720 ()
{
  int adr;

  trace_input ("ld.w", OP_LOAD32, 4);

  adr = State.regs[ OP[0] ] + SEXT16 (OP[2] & ~1);
  adr &= ~0x3;
  
  State.regs[ OP[1] ] = load_mem (adr, 4);
  
  trace_output (OP_LOAD32);

  return 4;
}

/* st.b */
int
OP_740 ()
{
  trace_input ("st.b", OP_STORE32, 1);

  store_mem (State.regs[ OP[0] ] + SEXT16 (OP[2]), 1, State.regs[ OP[1] ]);
  
  trace_output (OP_STORE32);

  return 4;
}

/* st.h */
int
OP_760 ()
{
  int adr;
  
  trace_input ("st.h", OP_STORE32, 2);

  adr = State.regs[ OP[0] ] + SEXT16 (OP[2]);
  adr &= ~1;
  
  store_mem (adr, 2, State.regs[ OP[1] ]);
  
  trace_output (OP_STORE32);

  return 4;
}

/* st.w */
int
OP_10760 ()
{
  int adr;
  
  trace_input ("st.w", OP_STORE32, 4);

  adr = State.regs[ OP[0] ] + SEXT16 (OP[2] & ~1);
  adr &= ~3;
  
  store_mem (adr, 4, State.regs[ OP[1] ]);
  
  trace_output (OP_STORE32);

  return 4;
}

static int
branch (int code)
{
  unsigned int psw;
  int op0;

  trace_input ("Bcond", OP_COND_BR, 0);
  trace_output (OP_COND_BR);

  if (condition_met (code))
    return SEXT9 (((OP[3] & 0x70) >> 3) | ((OP[3] & 0xf800) >> 7));
  else
    return 2;
}

/* bv disp9 */
int
OP_580 ()
{
  return branch (0);
}

/* bl disp9 */
int
OP_581 ()
{
  return branch (1);
}

/* be disp9 */
int
OP_582 ()
{
  return branch (2);
}

/* bnh disp 9*/
int
OP_583 ()
{
  return branch (3);
}

/* bn disp9 */
int
OP_584 ()
{
  return branch (4);
}

/* br disp9 */
int
OP_585 ()
{
  return branch (5);
}

/* blt disp9 */
int
OP_586 ()
{
  return branch (6);
}

/* ble disp9 */
int
OP_587 ()
{
  return branch (7);
}

/* bnv disp9 */
int
OP_588 ()
{
  return branch (8);
}

/* bnl disp9 */
int
OP_589 ()
{
  return branch (9);
}

/* bne disp9 */
int
OP_58A ()
{
  return branch (10);
}

/* bh disp9 */
int
OP_58B ()
{
  return branch (11);
}

/* bp disp9 */
int
OP_58C ()
{
  return branch (12);
}

/* bsa disp9 */
int
OP_58D ()
{
  return branch (13);
}

/* bge disp9 */
int
OP_58E ()
{
  return branch (14);
}

/* bgt disp9 */
int
OP_58F ()
{
  return branch (15);
}

/* jmp [reg1] */
/* sld.bu disp4[ep], reg2 */
int
OP_60 ()
{
  if (OP[1] == 0)
    {
      trace_input ("jmp", OP_REG, 0);
      
      PC = State.regs[ OP[0] ];
      
      trace_output (OP_REG);

      return 0; /* Add nothing to the PC, we have already done it.  */
    }
/* start-sanitize-v850e */
  else
    {
      unsigned long result;
      
      result = load_mem (State.regs[30] + (OP[3] & 0xf), 1);
      
/* start-sanitize-v850eq */
#ifdef ARCH_v850eq
      trace_input ("sld.b", OP_LOAD16, 1);
      
      State.regs[ OP[1] ] = SEXT8 (result);
#else
/* end-sanitize-v850eq */
      trace_input ("sld.bu", OP_LOAD16, 1);
      
      State.regs[ OP[1] ] = result;
/* start-sanitize-v850eq */
#endif
/* end-sanitize-v850eq */
      
      trace_output (OP_LOAD16);
      
      return 2;
    }
/* end-sanitize-v850e */
}

/* jarl/jr disp22, reg */
int
OP_780 ()
{
  trace_input ("jarl/jr", OP_JUMP, 0);

  if (OP[ 1 ] != 0)
    State.regs[ OP[1] ] = PC + 4;
  
  trace_output (OP_JUMP);
  
  return SEXT22 (((OP[3] & 0x3f) << 16) | OP[2]);
}

/* add reg, reg */
int
OP_1C0 ()
{
  unsigned int op0, op1, result, z, s, cy, ov;

  trace_input ("add", OP_REG_REG, 0);
  
  /* Compute the result.  */
  
  op0 = State.regs[ OP[0] ];
  op1 = State.regs[ OP[1] ];
  
  result = op0 + op1;

  /* Compute the condition codes.  */
  z = (result == 0);
  s = (result & 0x80000000);
  cy = (result < op0 || result < op1);
  ov = ((op0 & 0x80000000) == (op1 & 0x80000000)
	&& (op0 & 0x80000000) != (result & 0x80000000));

  /* Store the result and condition codes.  */
  State.regs[OP[1]] = result;
  PSW &= ~(PSW_Z | PSW_S | PSW_CY | PSW_OV);
  PSW |= ((z ? PSW_Z : 0) | (s ? PSW_S : 0)
		     | (cy ? PSW_CY : 0) | (ov ? PSW_OV : 0));
  trace_output (OP_REG_REG);

  return 2;
}

/* add sign_extend(imm5), reg */
int
OP_240 ()
{
  unsigned int op0, op1, result, z, s, cy, ov;
  int temp;

  trace_input ("add", OP_IMM_REG, 0);

  /* Compute the result.  */
  temp = SEXT5 (OP[0]);
  op0 = temp;
  op1 = State.regs[OP[1]];
  result = op0 + op1;
  
  /* Compute the condition codes.  */
  z = (result == 0);
  s = (result & 0x80000000);
  cy = (result < op0 || result < op1);
  ov = ((op0 & 0x80000000) == (op1 & 0x80000000)
	&& (op0 & 0x80000000) != (result & 0x80000000));

  /* Store the result and condition codes.  */
  State.regs[OP[1]] = result;
  PSW &= ~(PSW_Z | PSW_S | PSW_CY | PSW_OV);
  PSW |= ((z ? PSW_Z : 0) | (s ? PSW_S : 0)
		| (cy ? PSW_CY : 0) | (ov ? PSW_OV : 0));
  trace_output (OP_IMM_REG);

  return 2;
}

/* addi sign_extend(imm16), reg, reg */
int
OP_600 ()
{
  unsigned int op0, op1, result, z, s, cy, ov;

  trace_input ("addi", OP_IMM_REG_REG, 0);

  /* Compute the result.  */

  op0 = SEXT16 (OP[2]);
  op1 = State.regs[ OP[0] ];
  result = op0 + op1;
  
  /* Compute the condition codes.  */
  z = (result == 0);
  s = (result & 0x80000000);
  cy = (result < op0 || result < op1);
  ov = ((op0 & 0x80000000) == (op1 & 0x80000000)
	&& (op0 & 0x80000000) != (result & 0x80000000));

  /* Store the result and condition codes.  */
  State.regs[OP[1]] = result;
  PSW &= ~(PSW_Z | PSW_S | PSW_CY | PSW_OV);
  PSW |= ((z ? PSW_Z : 0) | (s ? PSW_S : 0)
		| (cy ? PSW_CY : 0) | (ov ? PSW_OV : 0));
  trace_output (OP_IMM_REG_REG);

  return 4;
}

/* sub reg1, reg2 */
int
OP_1A0 ()
{
  unsigned int op0, op1, result, z, s, cy, ov;

  trace_input ("sub", OP_REG_REG, 0);
  /* Compute the result.  */
  op0 = State.regs[ OP[0] ];
  op1 = State.regs[ OP[1] ];
  result = op1 - op0;

  /* Compute the condition codes.  */
  z = (result == 0);
  s = (result & 0x80000000);
  cy = (op1 < op0);
  ov = ((op1 & 0x80000000) != (op0 & 0x80000000)
	&& (op1 & 0x80000000) != (result & 0x80000000));

  /* Store the result and condition codes.  */
  State.regs[OP[1]] = result;
  PSW &= ~(PSW_Z | PSW_S | PSW_CY | PSW_OV);
  PSW |= ((z ? PSW_Z : 0) | (s ? PSW_S : 0)
		| (cy ? PSW_CY : 0) | (ov ? PSW_OV : 0));
  trace_output (OP_REG_REG);

  return 2;
}

/* subr reg1, reg2 */
int
OP_180 ()
{
  unsigned int op0, op1, result, z, s, cy, ov;

  trace_input ("subr", OP_REG_REG, 0);
  /* Compute the result.  */
  op0 = State.regs[ OP[0] ];
  op1 = State.regs[ OP[1] ];
  result = op0 - op1;

  /* Compute the condition codes.  */
  z = (result == 0);
  s = (result & 0x80000000);
  cy = (op0 < op1);
  ov = ((op0 & 0x80000000) != (op1 & 0x80000000)
	&& (op0 & 0x80000000) != (result & 0x80000000));

  /* Store the result and condition codes.  */
  State.regs[OP[1]] = result;
  PSW &= ~(PSW_Z | PSW_S | PSW_CY | PSW_OV);
  PSW |= ((z ? PSW_Z : 0) | (s ? PSW_S : 0)
		| (cy ? PSW_CY : 0) | (ov ? PSW_OV : 0));
  trace_output (OP_REG_REG);

  return 2;
}

/* sxh reg1 */
/* mulh reg1, reg2 */
int
OP_E0 ()
{
/* start-sanitize-v850e */
  if (OP[1] == 0)
    {
      trace_input ("sxh", OP_REG, 0);
      
      State.regs[ OP[0] ] = SEXT16 (State.regs[ OP[0] ]);

      trace_output (OP_REG);
    }
  else
/* end-sanitize-v850e */
    {
      trace_input ("mulh", OP_REG_REG, 0);
      
      State.regs[ OP[1] ] = (SEXT16 (State.regs[ OP[1] ]) * SEXT16 (State.regs[ OP[0] ]));
      
      trace_output (OP_REG_REG);
    }

  return 2;
}

/* mulh sign_extend(imm5), reg2 */
int
OP_2E0 ()
{
  trace_input ("mulh", OP_IMM_REG, 0);
  
  State.regs[ OP[1] ] = SEXT16 (State.regs[ OP[1] ]) * SEXT5 (OP[0]);
  
  trace_output (OP_IMM_REG);

  return 2;
}

/* mulhi imm16, reg1, reg2 */
int
OP_6E0 ()
{
  if (OP[1] == 0)
    {
    }
  else
    {
      trace_input ("mulhi", OP_IMM_REG_REG, 0);
  
      State.regs[ OP[1] ] = SEXT16 (State.regs[ OP[0] ]) * SEXT16 (OP[2]);
      
      trace_output (OP_IMM_REG_REG);
    }
  
  return 4;
}

/* divh reg1, reg2 */
/* switch  reg1 */
int
OP_40 ()
{
/* start-sanitize-v850e */
  if (OP[1] == 0)
    {
      unsigned long adr;

      trace_input ("switch", OP_REG, 0);
      
      adr      = State.pc + 2 + (State.regs[ OP[0] ] << 1);
      State.pc = State.pc + 2 + (SEXT16 (load_mem (adr, 2)) << 1);

      trace_output (OP_REG);
    }
  else
/* end-sanitize-v850e */
    {
      unsigned int op0, op1, result, ov, s, z;
      int temp;

      trace_input ("divh", OP_REG_REG, 0);

      /* Compute the result.  */
      temp = SEXT16 (State.regs[ OP[0] ]);
      op0 = temp;
      op1 = State.regs[OP[1]];
      
      if (op0 == 0xffffffff && op1 == 0x80000000)
	{
	  result = 0x80000000;
	  ov = 1;
	}
      else if (op0 != 0)
	{
	  result = op1 / op0;
	  ov = 0;
	}
      else
	{
	  result = 0x0;
	  ov = 1;
	}
      
      /* Compute the condition codes.  */
      z = (result == 0);
      s = (result & 0x80000000);
      
      /* Store the result and condition codes.  */
      State.regs[OP[1]] = result;
      PSW &= ~(PSW_Z | PSW_S | PSW_OV);
      PSW |= ((z ? PSW_Z : 0) | (s ? PSW_S : 0)
	      | (ov ? PSW_OV : 0));
      trace_output (OP_REG_REG);
    }

  return 2;
}

/* cmp reg, reg */
int
OP_1E0 ()
{
  unsigned int op0, op1, result, z, s, cy, ov;

  trace_input ("cmp", OP_REG_REG_CMP, 0);
  /* Compute the result.  */
  op0 = State.regs[ OP[0] ];
  op1 = State.regs[ OP[1] ];
  result = op1 - op0;

  /* Compute the condition codes.  */
  z = (result == 0);
  s = (result & 0x80000000);
  cy = (op1 < op0);
  ov = ((op1 & 0x80000000) != (op0 & 0x80000000)
	&& (op1 & 0x80000000) != (result & 0x80000000));

  /* Set condition codes.  */
  PSW &= ~(PSW_Z | PSW_S | PSW_CY | PSW_OV);
  PSW |= ((z ? PSW_Z : 0) | (s ? PSW_S : 0)
		| (cy ? PSW_CY : 0) | (ov ? PSW_OV : 0));
  trace_output (OP_REG_REG_CMP);

  return 2;
}

/* cmp sign_extend(imm5), reg */
int
OP_260 ()
{
  unsigned int op0, op1, result, z, s, cy, ov;
  int temp;

  /* Compute the result.  */
  trace_input ("cmp", OP_IMM_REG_CMP, 0);
  temp = SEXT5 (OP[0]);
  op0 = temp;
  op1 = State.regs[OP[1]];
  result = op1 - op0;

  /* Compute the condition codes.  */
  z = (result == 0);
  s = (result & 0x80000000);
  cy = (op1 < op0);
  ov = ((op1 & 0x80000000) != (op0 & 0x80000000)
	&& (op1 & 0x80000000) != (result & 0x80000000));

  /* Set condition codes.  */
  PSW &= ~(PSW_Z | PSW_S | PSW_CY | PSW_OV);
  PSW |= ((z ? PSW_Z : 0) | (s ? PSW_S : 0)
		| (cy ? PSW_CY : 0) | (ov ? PSW_OV : 0));
  trace_output (OP_IMM_REG_CMP);

  return 2;
}

/* setf cccc,reg2 */
int
OP_7E0 ()
{
  trace_input ("setf", OP_EX1, 0);

  State.regs[ OP[1] ] = condition_met (OP[0]);
  
  trace_output (OP_EX1);

  return 4;
}

/* zxh reg1 */
/* satadd reg,reg */
int
OP_C0 ()
{
/* start-sanitize-v850e */
  if (OP[1] == 0)
    {
      trace_input ("zxh", OP_REG, 0);
      
      State.regs[ OP[0] ] &= 0xffff;

      trace_output (OP_REG);
    }
  else
/* end-sanitize-v850e */
    {
      unsigned int op0, op1, result, z, s, cy, ov, sat;

      trace_input ("satadd", OP_REG_REG, 0);
      /* Compute the result.  */
      op0 = State.regs[ OP[0] ];
      op1 = State.regs[ OP[1] ];
      result = op0 + op1;
      
      /* Compute the condition codes.  */
      z = (result == 0);
      s = (result & 0x80000000);
      cy = (result < op0 || result < op1);
      ov = ((op0 & 0x80000000) == (op1 & 0x80000000)
	    && (op0 & 0x80000000) != (result & 0x80000000));
      sat = ov;
      
      /* Store the result and condition codes.  */
      State.regs[OP[1]] = result;
      PSW &= ~(PSW_Z | PSW_S | PSW_CY | PSW_OV);
      PSW |= ((z ? PSW_Z : 0) | (s ? PSW_S : 0)
	      | (cy ? PSW_CY : 0) | (ov ? PSW_OV : 0)
	      | (sat ? PSW_SAT : 0));
      
      /* Handle saturated results.  */
      if (sat && s)
	State.regs[OP[1]] = 0x80000000;
      else if (sat)
	State.regs[OP[1]] = 0x7fffffff;
      trace_output (OP_REG_REG);
    }

  return 2;
}

/* satadd sign_extend(imm5), reg */
int
OP_220 ()
{
  unsigned int op0, op1, result, z, s, cy, ov, sat;

  int temp;

  trace_input ("satadd", OP_IMM_REG, 0);

  /* Compute the result.  */
  temp = SEXT5 (OP[0]);
  op0 = temp;
  op1 = State.regs[OP[1]];
  result = op0 + op1;

  /* Compute the condition codes.  */
  z = (result == 0);
  s = (result & 0x80000000);
  cy = (result < op0 || result < op1);
  ov = ((op0 & 0x80000000) == (op1 & 0x80000000)
	&& (op0 & 0x80000000) != (result & 0x80000000));
  sat = ov;

  /* Store the result and condition codes.  */
  State.regs[OP[1]] = result;
  PSW &= ~(PSW_Z | PSW_S | PSW_CY | PSW_OV);
  PSW |= ((z ? PSW_Z : 0) | (s ? PSW_S : 0)
		| (cy ? PSW_CY : 0) | (ov ? PSW_OV : 0)
		| (sat ? PSW_SAT : 0));

  /* Handle saturated results.  */
  if (sat && s)
    State.regs[OP[1]] = 0x80000000;
  else if (sat)
    State.regs[OP[1]] = 0x7fffffff;
  trace_output (OP_IMM_REG);

  return 2;
}

/* satsub reg1, reg2 */
/* sxb reg1 */
int
OP_A0 ()
{
/* start-sanitize-v850e */
  if (OP[1] == 0)
    {
      trace_input ("sxb", OP_REG, 0);

      State.regs[ OP[0] ] = SEXT8 (State.regs[ OP[0] ]);

      trace_output (OP_REG);
    }
  else
/* end-sanitize-v850e */
    {
      unsigned int op0, op1, result, z, s, cy, ov, sat;

      trace_input ("satsub", OP_REG_REG, 0);
      
      /* Compute the result.  */
      op0 = State.regs[ OP[0] ];
      op1 = State.regs[ OP[1] ];
      result = op1 - op0;
      
      /* Compute the condition codes.  */
      z = (result == 0);
      s = (result & 0x80000000);
      cy = (op1 < op0);
      ov = ((op1 & 0x80000000) != (op0 & 0x80000000)
	    && (op1 & 0x80000000) != (result & 0x80000000));
      sat = ov;
      
      /* Store the result and condition codes.  */
      State.regs[OP[1]] = result;
      PSW &= ~(PSW_Z | PSW_S | PSW_CY | PSW_OV);
      PSW |= ((z ? PSW_Z : 0) | (s ? PSW_S : 0)
	      | (cy ? PSW_CY : 0) | (ov ? PSW_OV : 0)
	      | (sat ? PSW_SAT : 0));
      
      /* Handle saturated results.  */
      if (sat && s)
	State.regs[OP[1]] = 0x80000000;
      else if (sat)
	State.regs[OP[1]] = 0x7fffffff;
      trace_output (OP_REG_REG);
    }

  return 2;
}

/* satsubi sign_extend(imm16), reg */
int
OP_660 ()
{
  unsigned int op0, op1, result, z, s, cy, ov, sat;
  int temp;

  trace_input ("satsubi", OP_IMM_REG, 0);

  /* Compute the result.  */
  temp = SEXT16 (OP[2]);
  op0 = temp;
  op1 = State.regs[ OP[0] ];
  result = op1 - op0;

  /* Compute the condition codes.  */
  z = (result == 0);
  s = (result & 0x80000000);
  cy = (op1 < op0);
  ov = ((op1 & 0x80000000) != (op0 & 0x80000000)
	&& (op1 & 0x80000000) != (result & 0x80000000));
  sat = ov;

  /* Store the result and condition codes.  */
  State.regs[OP[1]] = result;
  PSW &= ~(PSW_Z | PSW_S | PSW_CY | PSW_OV);
  PSW |= ((z ? PSW_Z : 0) | (s ? PSW_S : 0)
		| (cy ? PSW_CY : 0) | (ov ? PSW_OV : 0)
		| (sat ? PSW_SAT : 0));

  /* Handle saturated results.  */
  if (sat && s)
    State.regs[OP[1]] = 0x80000000;
  else if (sat)
    State.regs[OP[1]] = 0x7fffffff;
  trace_output (OP_IMM_REG);

  return 4;
}

/* satsubr reg,reg */
/* zxb reg1 */
int
OP_80 ()
{
/* start-sanitize-v850e */
  if (OP[1] == 0)
    {
      trace_input ("zxb", OP_REG, 0);

      State.regs[ OP[0] ] &= 0xff;

      trace_output (OP_REG);
    }
  else
/* end-sanitize-v850e */
    {
      unsigned int op0, op1, result, z, s, cy, ov, sat;
      
      trace_input ("satsubr", OP_REG_REG, 0);
      
      /* Compute the result.  */
      op0 = State.regs[ OP[0] ];
      op1 = State.regs[ OP[1] ];
      result = op0 - op1;
      
      /* Compute the condition codes.  */
      z = (result == 0);
      s = (result & 0x80000000);
      cy = (result < op0);
      ov = ((op1 & 0x80000000) != (op0 & 0x80000000)
	    && (op1 & 0x80000000) != (result & 0x80000000));
      sat = ov;
      
      /* Store the result and condition codes.  */
      State.regs[OP[1]] = result;
      PSW &= ~(PSW_Z | PSW_S | PSW_CY | PSW_OV);
      PSW |= ((z ? PSW_Z : 0) | (s ? PSW_S : 0)
	      | (cy ? PSW_CY : 0) | (ov ? PSW_OV : 0)
	      | (sat ? PSW_SAT : 0));
      
      /* Handle saturated results.  */
      if (sat && s)
	State.regs[OP[1]] = 0x80000000;
      else if (sat)
	State.regs[OP[1]] = 0x7fffffff;
      trace_output (OP_REG_REG);
    }

  return 2;
}

/* tst reg,reg */
int
OP_160 ()
{
  unsigned int op0, op1, result, z, s;

  trace_input ("tst", OP_REG_REG_CMP, 0);

  /* Compute the result.  */
  op0 = State.regs[ OP[0] ];
  op1 = State.regs[ OP[1] ];
  result = op0 & op1;

  /* Compute the condition codes.  */
  z = (result == 0);
  s = (result & 0x80000000);

  /* Store the condition codes.  */
  PSW &= ~(PSW_Z | PSW_S | PSW_OV);
  PSW |= ((z ? PSW_Z : 0) | (s ? PSW_S : 0));
  trace_output (OP_REG_REG_CMP);

  return 2;
}

/* mov reg, reg */
int
OP_0 ()
{
  trace_input ("mov", OP_REG_REG_MOVE, 0);
  
  State.regs[ OP[1] ] = State.regs[ OP[0] ];
  
  trace_output (OP_REG_REG_MOVE);

  return 2;
}

/* mov sign_extend(imm5), reg */
/* callt imm6 */
int
OP_200 ()
{
/* start-sanitize-v850e */
  if (OP[1] == 0)
    {
      unsigned long adr;
      
      trace_input ("callt", OP_LOAD16, 1);
      
      CTPC  = PC + 2;
      CTPSW = PSW;
      
      adr = CTBP + ((OP[3] & 0x3f) << 1);
      
      PC = CTBP + load_mem (adr, 1);

      trace_output (OP_LOAD16);

      return 0;
    }
  else
/* end-sanitize-v850e */
    {
      int value = SEXT5 (OP[0]);
 
      trace_input ("mov", OP_IMM_REG_MOVE, 0);
      
      State.regs[ OP[1] ] = value;
      
      trace_output (OP_IMM_REG_MOVE);

      return 2;
    }
}

/* mov imm32, reg1 */
/* movea sign_extend(imm16), reg, reg  */
int
OP_620 ()
{
/* start-sanitize-v850e */
  if (OP[1] == 0)
    {
      trace_input ("mov", OP_IMM32_REG, 4);

      State.regs[ OP[0] ] = load_mem (PC + 2, 4);
      
      trace_output (OP_IMM32_REG);

      return 6;
    }
  else
/* end-sanitize-v850e */
    {
      trace_input ("movea", OP_IMM_REG_REG, 0);
  
      State.regs[ OP[1] ] = State.regs[ OP[0] ] + SEXT16 (OP[2]);
  
      trace_output (OP_IMM_REG_REG);

      return 4;
    }
}

/* dispose imm5, list12 [, reg1] */
/* movhi imm16, reg, reg */
int
OP_640 ()
{
/* start-sanitize-v850e */

  if (OP[1] == 0)
    {
      int        i;
      
      trace_input ("dispose", OP_PUSHPOP1, 0);

      SP += (OP[3] & 0x3e) << 1;

      /* Load the registers with lower number registers being retrieved from higher addresses.  */
      for (i = 12; i--;)
	if ((OP[3] & (1 << type1_regs[ i ])))
	  {
	    State.regs[ 20 + i ] = load_mem (SP, 4);
	    SP += 4;
	  }

      if ((OP[3] & 0x1f0000) != 0)
	{
	  PC = State.regs[ (OP[3] >> 16) & 0x1f];
	  return 0;
	}
      
      trace_output (OP_PUSHPOP1);
    }
  else
/* end-sanitize-v850e */
    {
      trace_input ("movhi", OP_UIMM_REG_REG, 16);
      
      State.regs[ OP[1] ] = State.regs[ OP[0] ] + OP[2] << 16;
      
      trace_output (OP_UIMM_REG_REG);
    }

  return 4;
}

/* sar zero_extend(imm5),reg1 */
int
OP_2A0 ()
{
  unsigned int op0, op1, result, z, s, cy;

  trace_input ("sar", OP_IMM_REG, 0);
  op0 = OP[0];
  op1 = State.regs[ OP[1] ];
  result = (signed)op1 >> op0;

  /* Compute the condition codes.  */
  z = (result == 0);
  s = (result & 0x80000000);
  cy = (op1 & (1 << (op0 - 1)));

  /* Store the result and condition codes.  */
  State.regs[ OP[1] ] = result;
  PSW &= ~(PSW_Z | PSW_S | PSW_OV | PSW_CY);
  PSW |= ((z ? PSW_Z : 0) | (s ? PSW_S : 0)
		| (cy ? PSW_CY : 0));
  trace_output (OP_IMM_REG);

  return 2;
}

/* sar reg1, reg2 */
int
OP_A007E0 ()
{
  unsigned int op0, op1, result, z, s, cy;

  trace_input ("sar", OP_REG_REG, 0);
  
  op0 = State.regs[ OP[0] ] & 0x1f;
  op1 = State.regs[ OP[1] ];
  result = (signed)op1 >> op0;

  /* Compute the condition codes.  */
  z = (result == 0);
  s = (result & 0x80000000);
  cy = (op1 & (1 << (op0 - 1)));

  /* Store the result and condition codes.  */
  State.regs[OP[1]] = result;
  PSW &= ~(PSW_Z | PSW_S | PSW_OV | PSW_CY);
  PSW |= ((z ? PSW_Z : 0) | (s ? PSW_S : 0)
		| (cy ? PSW_CY : 0));
  trace_output (OP_REG_REG);

  return 4;
}

/* shl zero_extend(imm5),reg1 */
int
OP_2C0 ()
{
  unsigned int op0, op1, result, z, s, cy;

  trace_input ("shl", OP_IMM_REG, 0);
  op0 = OP[0];
  op1 = State.regs[ OP[1] ];
  result = op1 << op0;

  /* Compute the condition codes.  */
  z = (result == 0);
  s = (result & 0x80000000);
  cy = (op1 & (1 << (32 - op0)));

  /* Store the result and condition codes.  */
  State.regs[OP[1]] = result;
  PSW &= ~(PSW_Z | PSW_S | PSW_OV | PSW_CY);
  PSW |= ((z ? PSW_Z : 0) | (s ? PSW_S : 0)
		| (cy ? PSW_CY : 0));
  trace_output (OP_IMM_REG);

  return 2;
}

/* shl reg1, reg2 */
int
OP_C007E0 ()
{
  unsigned int op0, op1, result, z, s, cy;

  trace_input ("shl", OP_REG_REG, 0);
  op0 = State.regs[ OP[0] ] & 0x1f;
  op1 = State.regs[ OP[1] ];
  result = op1 << op0;

  /* Compute the condition codes.  */
  z = (result == 0);
  s = (result & 0x80000000);
  cy = (op1 & (1 << (32 - op0)));

  /* Store the result and condition codes.  */
  State.regs[OP[1]] = result;
  PSW &= ~(PSW_Z | PSW_S | PSW_OV | PSW_CY);
  PSW |= ((z ? PSW_Z : 0) | (s ? PSW_S : 0)
		| (cy ? PSW_CY : 0));
  trace_output (OP_REG_REG);

  return 4;
}

/* shr zero_extend(imm5),reg1 */
int
OP_280 ()
{
  unsigned int op0, op1, result, z, s, cy;

  trace_input ("shr", OP_IMM_REG, 0);
  op0 = OP[0];
  op1 = State.regs[ OP[1] ];
  result = op1 >> op0;

  /* Compute the condition codes.  */
  z = (result == 0);
  s = (result & 0x80000000);
  cy = (op1 & (1 << (op0 - 1)));

  /* Store the result and condition codes.  */
  State.regs[OP[1]] = result;
  PSW &= ~(PSW_Z | PSW_S | PSW_OV | PSW_CY);
  PSW |= ((z ? PSW_Z : 0) | (s ? PSW_S : 0)
		| (cy ? PSW_CY : 0));
  trace_output (OP_IMM_REG);

  return 2;
}

/* shr reg1, reg2 */
int
OP_8007E0 ()
{
  unsigned int op0, op1, result, z, s, cy;

  trace_input ("shr", OP_REG_REG, 0);
  op0 = State.regs[ OP[0] ] & 0x1f;
  op1 = State.regs[ OP[1] ];
  result = op1 >> op0;

  /* Compute the condition codes.  */
  z = (result == 0);
  s = (result & 0x80000000);
  cy = (op1 & (1 << (op0 - 1)));

  /* Store the result and condition codes.  */
  State.regs[OP[1]] = result;
  PSW &= ~(PSW_Z | PSW_S | PSW_OV | PSW_CY);
  PSW |= ((z ? PSW_Z : 0) | (s ? PSW_S : 0)
		| (cy ? PSW_CY : 0));
  trace_output (OP_REG_REG);

  return 4;
}

/* or reg, reg */
int
OP_100 ()
{
  unsigned int op0, op1, result, z, s;

  trace_input ("or", OP_REG_REG, 0);

  /* Compute the result.  */
  op0 = State.regs[ OP[0] ];
  op1 = State.regs[ OP[1] ];
  result = op0 | op1;

  /* Compute the condition codes.  */
  z = (result == 0);
  s = (result & 0x80000000);

  /* Store the result and condition codes.  */
  State.regs[OP[1]] = result;
  PSW &= ~(PSW_Z | PSW_S | PSW_OV);
  PSW |= ((z ? PSW_Z : 0) | (s ? PSW_S : 0));
  trace_output (OP_REG_REG);

  return 2;
}

/* ori zero_extend(imm16), reg, reg */
int
OP_680 ()
{
  unsigned int op0, op1, result, z, s;

  trace_input ("ori", OP_UIMM_REG_REG, 0);
  op0 = OP[2];
  op1 = State.regs[ OP[0] ];
  result = op0 | op1;

  /* Compute the condition codes.  */
  z = (result == 0);
  s = (result & 0x80000000);

  /* Store the result and condition codes.  */
  State.regs[OP[1]] = result;
  PSW &= ~(PSW_Z | PSW_S | PSW_OV);
  PSW |= ((z ? PSW_Z : 0) | (s ? PSW_S : 0));
  trace_output (OP_UIMM_REG_REG);

  return 4;
}

/* and reg, reg */
int
OP_140 ()
{
  unsigned int op0, op1, result, z, s;

  trace_input ("and", OP_REG_REG, 0);

  /* Compute the result.  */
  op0 = State.regs[ OP[0] ];
  op1 = State.regs[ OP[1] ];
  result = op0 & op1;

  /* Compute the condition codes.  */
  z = (result == 0);
  s = (result & 0x80000000);

  /* Store the result and condition codes.  */
  State.regs[OP[1]] = result;
  PSW &= ~(PSW_Z | PSW_S | PSW_OV);
  PSW |= ((z ? PSW_Z : 0) | (s ? PSW_S : 0));
  trace_output (OP_REG_REG);

  return 2;
}

/* andi zero_extend(imm16), reg, reg */
int
OP_6C0 ()
{
  unsigned int result, z;

  trace_input ("andi", OP_UIMM_REG_REG, 0);

  result = OP[2] & State.regs[ OP[0] ];

  /* Compute the condition codes.  */
  z = (result == 0);

  /* Store the result and condition codes.  */
  State.regs[ OP[1] ] = result;
  
  PSW &= ~(PSW_Z | PSW_S | PSW_OV);
  PSW |= (z ? PSW_Z : 0);
  
  trace_output (OP_UIMM_REG_REG);

  return 4;
}

/* xor reg, reg */
int
OP_120 ()
{
  unsigned int op0, op1, result, z, s;

  trace_input ("xor", OP_REG_REG, 0);

  /* Compute the result.  */
  op0 = State.regs[ OP[0] ];
  op1 = State.regs[ OP[1] ];
  result = op0 ^ op1;

  /* Compute the condition codes.  */
  z = (result == 0);
  s = (result & 0x80000000);

  /* Store the result and condition codes.  */
  State.regs[OP[1]] = result;
  PSW &= ~(PSW_Z | PSW_S | PSW_OV);
  PSW |= ((z ? PSW_Z : 0) | (s ? PSW_S : 0));
  trace_output (OP_REG_REG);

  return 2;
}

/* xori zero_extend(imm16), reg, reg */
int
OP_6A0 ()
{
  unsigned int op0, op1, result, z, s;

  trace_input ("xori", OP_UIMM_REG_REG, 0);
  op0 = OP[2];
  op1 = State.regs[ OP[0] ];
  result = op0 ^ op1;

  /* Compute the condition codes.  */
  z = (result == 0);
  s = (result & 0x80000000);

  /* Store the result and condition codes.  */
  State.regs[OP[1]] = result;
  PSW &= ~(PSW_Z | PSW_S | PSW_OV);
  PSW |= ((z ? PSW_Z : 0) | (s ? PSW_S : 0));
  trace_output (OP_UIMM_REG_REG);

  return 4;
}

/* not reg1, reg2 */
int
OP_20 ()
{
  unsigned int op0, result, z, s;

  trace_input ("not", OP_REG_REG_MOVE, 0);
  /* Compute the result.  */
  op0 = State.regs[ OP[0] ];
  result = ~op0;

  /* Compute the condition codes.  */
  z = (result == 0);
  s = (result & 0x80000000);

  /* Store the result and condition codes.  */
  State.regs[OP[1]] = result;
  PSW &= ~(PSW_Z | PSW_S | PSW_OV);
  PSW |= ((z ? PSW_Z : 0) | (s ? PSW_S : 0));
  trace_output (OP_REG_REG_MOVE);

  return 2;
}

/* set1 */
int
OP_7C0 ()
{
  unsigned int op0, op1, op2;
  int temp;

  trace_input ("set1", OP_BIT, 0);
  op0 = State.regs[ OP[0] ];
  op1 = OP[1] & 0x7;
  temp = SEXT16 (OP[2]);
  op2 = temp;
  temp = load_mem (op0 + op2, 1);
  PSW &= ~PSW_Z;
  if ((temp & (1 << op1)) == 0)
    PSW |= PSW_Z;
  temp |= (1 << op1);
  store_mem (op0 + op2, 1, temp);
  trace_output (OP_BIT);

  return 4;
}

/* not1 */
int
OP_47C0 ()
{
  unsigned int op0, op1, op2;
  int temp;

  trace_input ("not1", OP_BIT, 0);
  op0 = State.regs[ OP[0] ];
  op1 = OP[1] & 0x7;
  temp = SEXT16 (OP[2]);
  op2 = temp;
  temp = load_mem (op0 + op2, 1);
  PSW &= ~PSW_Z;
  if ((temp & (1 << op1)) == 0)
    PSW |= PSW_Z;
  temp ^= (1 << op1);
  store_mem (op0 + op2, 1, temp);
  trace_output (OP_BIT);

  return 4;
}

/* clr1 */
int
OP_87C0 ()
{
  unsigned int op0, op1, op2;
  int temp;

  trace_input ("clr1", OP_BIT, 0);
  op0 = State.regs[ OP[0] ];
  op1 = OP[1] & 0x7;
  temp = SEXT16 (OP[2]);
  op2 = temp;
  temp = load_mem (op0 + op2, 1);
  PSW &= ~PSW_Z;
  if ((temp & (1 << op1)) == 0)
    PSW |= PSW_Z;
  temp &= ~(1 << op1);
  store_mem (op0 + op2, 1, temp);
  trace_output (OP_BIT);

  return 4;
}

/* tst1 */
int
OP_C7C0 ()
{
  unsigned int op0, op1, op2;
  int temp;

  trace_input ("tst1", OP_BIT, 0);
  op0 = State.regs[ OP[0] ];
  op1 = OP[1] & 0x7;
  temp = SEXT16 (OP[2]);
  op2 = temp;
  temp = load_mem (op0 + op2, 1);
  PSW &= ~PSW_Z;
  if ((temp & (1 << op1)) == 0)
    PSW |= PSW_Z;
  trace_output (OP_BIT);

  return 4;
}

/* breakpoint */
int
OP_FFFF ()
{
  State.exception = SIGTRAP;
  return  -4;
}

/* di */
int
OP_16007E0 ()
{
  trace_input ("di", OP_NONE, 0);
  PSW |= PSW_ID;
  trace_output (OP_NONE);

  return 4;
}

/* ei */
int
OP_16087E0 ()
{
  trace_input ("ei", OP_NONE, 0);
  PSW &= ~PSW_ID;
  trace_output (OP_NONE);

  return 4;
}

/* halt */
int
OP_12007E0 ()
{
  trace_input ("halt", OP_NONE, 0);
  /* FIXME this should put processor into a mode where NMI still handled */
  State.exception = SIGQUIT;
  trace_output (OP_NONE);

  return 4;
}

/* reti */
int
OP_14007E0 ()
{
  trace_input ("reti", OP_NONE, 0);
  trace_output (OP_NONE);

  /* Restore for NMI if only NP on, otherwise is interrupt or exception.  */
  if ((PSW & (PSW_NP | PSW_EP)) == PSW_NP)
    {
      PC = FEPC - 4;
      PSW = FEPSW;
    }
  else
    {
      PC = EIPC - 4;
      PSW = EIPSW;
    }

  return 0;
}

/* trap */
int
OP_10007E0 ()
{
  trace_input ("trap", OP_TRAP, 0);
  trace_output (OP_TRAP);

  /* Trap 31 is used for simulating OS I/O functions */

  if (OP[0] == 31)
    {
      int save_errno = errno;	
      errno = 0;

/* Registers passed to trap 0 */

#define FUNC   State.regs[6]	/* function number, return value */
#define PARM1  State.regs[7]	/* optional parm 1 */
#define PARM2  State.regs[8]	/* optional parm 2 */
#define PARM3  State.regs[9]	/* optional parm 3 */

/* Registers set by trap 0 */

#define RETVAL State.regs[10]	/* return value */
#define RETERR State.regs[11]	/* return error code */

/* Turn a pointer in a register into a pointer into real memory. */

#define MEMPTR(x) (map (x))

      switch (FUNC)
	{

#if !defined(__GO32__) && !defined(_WIN32)
#ifdef SYS_fork
	case SYS_fork:
	  RETVAL = fork ();
	  break;
#endif
#endif

#if !defined(__GO32__) && !defined(_WIN32)
#ifdef SYS_execv
	case SYS_execve:
	  RETVAL = execve (MEMPTR (PARM1), (char **) MEMPTR (PARM2),
			   (char **)MEMPTR (PARM3));
	  break;
#endif
#endif

#if !defined(__GO32__) && !defined(_WIN32)
#ifdef SYS_execv
	case SYS_execv:
	  RETVAL = execve (MEMPTR (PARM1), (char **) MEMPTR (PARM2), NULL);
	  break;
#endif
#endif

#if 0
#ifdef SYS_pipe
	case SYS_pipe:
	  {
	    reg_t buf;
	    int host_fd[2];

	    buf = PARM1;
	    RETVAL = pipe (host_fd);
	    SW (buf, host_fd[0]);
	    buf += sizeof(uint16);
	    SW (buf, host_fd[1]);
	  }
	  break;
#endif
#endif

#if 0
#ifdef SYS_wait
	case SYS_wait:
	  {
	    int status;

	    RETVAL = wait (&status);
	    SW (PARM1, status);
	  }
	  break;
#endif
#endif

#ifdef SYS_read
	case SYS_read:
	  RETVAL = v850_callback->read (v850_callback, PARM1, MEMPTR (PARM2),
					PARM3);
	  break;
#endif

#ifdef SYS_write
	case SYS_write:
	  if (PARM1 == 1)
	    RETVAL = (int)v850_callback->write_stdout (v850_callback,
				 		       MEMPTR (PARM2), PARM3);
	  else
	    RETVAL = (int)v850_callback->write (v850_callback, PARM1,
						MEMPTR (PARM2), PARM3);
	  break;
#endif

#ifdef SYS_lseek
	case SYS_lseek:
	  RETVAL = v850_callback->lseek (v850_callback, PARM1, PARM2, PARM3);
	  break;
#endif

#ifdef SYS_close
	case SYS_close:
	  RETVAL = v850_callback->close (v850_callback, PARM1);
	  break;
#endif

#ifdef SYS_open
	case SYS_open:
	  RETVAL = v850_callback->open (v850_callback, MEMPTR (PARM1), PARM2);
	  break;
#endif

#ifdef SYS_exit
	case SYS_exit:
	  if ((PARM1 & 0xffff0000) == 0xdead0000 && (PARM1 & 0xffff) != 0)
	    State.exception = PARM1 & 0xffff;	/* get signal encoded by kill */
	  else if (PARM1 == 0xdead)
	    State.exception = SIGABRT;		/* old libraries */
	  else
	    State.exception = SIG_V850_EXIT;	/* PARM1 has exit status encoded */
	  break;
#endif

#if !defined(__GO32__) && !defined(_WIN32)
#ifdef SYS_stat
	case SYS_stat:	/* added at hmsi */
	  /* stat system call */
	  {
	    struct stat host_stat;
	    reg_t buf;

	    RETVAL = stat (MEMPTR (PARM1), &host_stat);

	    buf = PARM2;

	    /* Just wild-assed guesses.  */
	    store_mem (buf, 2, host_stat.st_dev);
	    store_mem (buf + 2, 2, host_stat.st_ino);
	    store_mem (buf + 4, 4, host_stat.st_mode);
	    store_mem (buf + 8, 2, host_stat.st_nlink);
	    store_mem (buf + 10, 2, host_stat.st_uid);
	    store_mem (buf + 12, 2, host_stat.st_gid);
	    store_mem (buf + 14, 2, host_stat.st_rdev);
	    store_mem (buf + 16, 4, host_stat.st_size);
	    store_mem (buf + 20, 4, host_stat.st_atime);
	    store_mem (buf + 28, 4, host_stat.st_mtime);
	    store_mem (buf + 36, 4, host_stat.st_ctime);
	  }
	  break;
#endif
#endif

#if !defined(__GO32__) && !defined(_WIN32)
#ifdef SYS_chown
	case SYS_chown:
	  RETVAL = chown (MEMPTR (PARM1), PARM2, PARM3);
	  break;
#endif
#endif

#ifdef SYS_chmod
#if HAVE_CHMOD
	case SYS_chmod:
	  RETVAL = chmod (MEMPTR (PARM1), PARM2);
	  break;
#endif
#endif

#ifdef SYS_time
#if HAVE_TIME
	case SYS_time:
	  {
	    time_t now;
	    RETVAL = time (&now);
	    store_mem (PARM1, 4, now);
	  }
	  break;
#endif
#endif

#if !defined(__GO32__) && !defined(_WIN32)
#ifdef SYS_times
	case SYS_times:
	  {
	    struct tms tms;
	    RETVAL = times (&tms);
	    store_mem (PARM1, 4, tms.tms_utime);
	    store_mem (PARM1 + 4, 4, tms.tms_stime);
	    store_mem (PARM1 + 8, 4, tms.tms_cutime);
	    store_mem (PARM1 + 12, 4, tms.tms_cstime);
	    break;
	  }
#endif
#endif

#ifdef SYS_gettimeofday
#if !defined(__GO32__) && !defined(_WIN32)
	case SYS_gettimeofday:
	  {
	    struct timeval t;
	    struct timezone tz;
	    RETVAL = gettimeofday (&t, &tz);
	    store_mem (PARM1, 4, t.tv_sec);
	    store_mem (PARM1 + 4, 4, t.tv_usec);
	    store_mem (PARM2, 4, tz.tz_minuteswest);
	    store_mem (PARM2 + 4, 4, tz.tz_dsttime);
	    break;
	  }
#endif
#endif

#if !defined(__GO32__) && !defined(_WIN32)
#ifdef SYS_utime
	case SYS_utime:
	  /* Cast the second argument to void *, to avoid type mismatch
	     if a prototype is present.  */
	  RETVAL = utime (MEMPTR (PARM1), (void *) MEMPTR (PARM2));
	  break;
#endif
#endif

	default:
	  abort ();
	}
      RETERR = errno;
      errno = save_errno;

      return 4;
    }
  else
    {				/* Trap 0 -> 30 */
      EIPC = PC + 4;
      EIPSW = PSW;
      /* Mask out EICC */
      ECR &= 0xffff0000;
      ECR |= 0x40 + OP[0];
      /* Flag that we are now doing exception processing.  */
      PSW |= PSW_EP | PSW_ID;
      PC = ((OP[0] < 0x10) ? 0x40 : 0x50) - 4;

      return 0;
    }
}

/* ldsr, reg,reg */
int
OP_2007E0 ()
{
  trace_input ("ldsr", OP_LDSR, 0);
  
  State.sregs[ OP[1] ] = State.regs[ OP[0] ];
  
  trace_output (OP_LDSR);

  return 4;
}

/* stsr */
int
OP_4007E0 ()
{
  unsigned int op0;

  trace_input ("stsr", OP_STSR, 0);
  
  State.regs[ OP[1] ] = State.sregs[ OP[0] ];
  
  trace_output (OP_STSR);

  return 4;
}

/* tst1 reg2, [reg1] */
int
OP_E607E0 (void)
{
  int temp;

  trace_input ("tst1", OP_BIT_LOAD, 1);

  temp = load_mem (State.regs[ OP[0] ], 1);
  
  PSW &= ~PSW_Z;
  if ((temp & (1 << State.regs[ OP[1] & 0x7 ])) == 0)
    PSW |= PSW_Z;
  
  trace_output (OP_BIT_LOAD);

  return 4;
}

/* mulu reg1, reg2, reg3 */
int
OP_22207E0 (void)
{
  trace_input ("mulu", OP_REG_REG_REG, 0);

  Multiply64 (false, State.regs[ OP[0] ]);

  trace_output (OP_REG_REG_REG);

  return 4;
}

/* start-sanitize-v850e */

#define BIT_CHANGE_OP( name, binop )		\
  unsigned int bit;				\
  unsigned int temp;				\
  						\
  trace_input (name, OP_BIT_CHANGE, 0);		\
  						\
  bit  = 1 << State.regs[ OP[1] & 0x7 ];	\
  temp = load_mem (State.regs[ OP[0] ], 1);	\
						\
  PSW &= ~PSW_Z;				\
  if ((temp & bit) == 0)			\
    PSW |= PSW_Z;				\
  temp binop bit;				\
  						\
  store_mem (State.regs[ OP[0] ], 1, temp);	\
	     					\
  trace_output (OP_BIT_CHANGE);			\
	     					\
  return 4;

/* clr1 reg2, [reg1] */
int
OP_E407E0 (void)
{
  BIT_CHANGE_OP ("clr1", &= ~ );
}

/* not1 reg2, [reg1] */
int
OP_E207E0 (void)
{
  BIT_CHANGE_OP ("not1", ^= );
}

/* set1 */
int
OP_E007E0 (void)
{
  BIT_CHANGE_OP ("set1", |= );
}

/* sasf */
int
OP_20007E0 (void)
{
  trace_input ("sasf", OP_EX1, 0);
  
  State.regs[ OP[1] ] = (State.regs[ OP[1] ] << 1) | condition_met (OP[0]);
  
  trace_output (OP_EX1);

  return 4;
}
/* end-sanitize-v850e */

/* start-sanitize-v850eq */
/* This function is courtesy of Sugimoto at NEC, via Seow Tan (Soew_Tan@el.nec.com) */
static void
divun
(
  unsigned int       N,
  unsigned long int  als,
  unsigned long int  sfi,
  unsigned long int *  quotient_ptr,
  unsigned long int *  remainder_ptr,
  boolean *          overflow_ptr
)
{
  unsigned long   ald = sfi >> N - 1;
  unsigned long   alo = als;
  unsigned int    Q   = 1;
  unsigned int    C;
  unsigned int    S   = 0;
  unsigned int    i;
  unsigned int    R1  = 1;
  unsigned int    OV;
  unsigned int    DBZ = (als == 0) ? 1 : 0;
  unsigned long   alt = Q ? ~als : als;

  /* 1st Loop */
  alo = ald + alt + Q;
  C   = (alt >> 31) & (ald >> 31) | ((alt >> 31) ^ (ald >> 31)) & (~alo >> 31);
  C   = C ^ Q;
  Q   = ~(C ^ S) & 1;
  R1  = (alo == 0) ? 0 : (R1 & Q);
  if ((S ^ (alo>>31)) && !C)
    {
      DBZ = 1;
    }
  S   = alo >> 31;
  sfi = (sfi << (32-N+1)) | Q;
  ald = (alo << 1) | (sfi >> 31);

  /* 2nd - N-1th Loop */
  for (i = 2; i < N; i++)
    {
      alt = Q ? ~als : als;
      alo = ald + alt + Q;
      C   = (alt >> 31) & (ald >> 31) | ((alt >> 31) ^ (ald >> 31)) & (~alo >> 31);
      C   = C ^ Q;
      Q   = ~(C ^ S) & 1;
      R1  = (alo == 0) ? 0 : (R1 & Q);
      if ((S ^ (alo>>31)) && !C && !DBZ)
	{
	  DBZ = 1;
	}
      S   = alo >> 31;
      sfi = (sfi << 1) | Q;
      ald = (alo << 1) | (sfi >> 31);
    }
  
  /* Nth Loop */
  alt = Q ? ~als : als;
  alo = ald + alt + Q;
  C   = (alt >> 31) & (ald >> 31) | ((alt >> 31) ^ (ald >> 31)) & (~alo >> 31);
  C   = C ^ Q;
  Q   = ~(C ^ S) & 1;
  R1  = (alo == 0) ? 0 : (R1 & Q);
  if ((S ^ (alo>>31)) && !C)
    {
      DBZ = 1;
    }
  
  * quotient_ptr  = (sfi << 1) | Q;
  * remainder_ptr = Q ? alo : (alo + als);
  * overflow_ptr  = DBZ | R1;
}

/* This function is courtesy of Sugimoto at NEC, via Seow Tan (Soew_Tan@el.nec.com) */
static void
divn
(
  unsigned int       N,
  unsigned long int  als,
  unsigned long int  sfi,
  signed long int *  quotient_ptr,
  signed long int *  remainder_ptr,
  boolean *          overflow_ptr
)
{
  unsigned long	  ald = (signed long) sfi >> (N - 1);
  unsigned long   alo = als;
  unsigned int    SS  = als >> 31;
  unsigned int	  SD  = sfi >> 31;
  unsigned int    R1  = 1;
  unsigned int    OV;
  unsigned int    DBZ = als == 0 ? 1 : 0;
  unsigned int    Q   = ~(SS ^ SD) & 1;
  unsigned int    C;
  unsigned int    S;
  unsigned int    i;
  unsigned long   alt = Q ? ~als : als;


  /* 1st Loop */
  
  alo = ald + alt + Q;
  C   = (alt >> 31) & (ald >> 31) | ((alt >> 31) ^ (ald >> 31)) & (~alo >> 31);
  Q   = C ^ SS;
  R1  = (alo == 0) ? 0 : (R1 & (Q ^ (SS ^ SD)));
  S   = alo >> 31;
  sfi = (sfi << (32-N+1)) | Q;
  ald = (alo << 1) | (sfi >> 31);
  if ((alo >> 31) ^ (ald >> 31))
    {
      DBZ = 1;
    }

  /* 2nd - N-1th Loop */
  
  for (i = 2; i < N; i++)
    {
      alt = Q ? ~als : als;
      alo = ald + alt + Q;
      C   = (alt >> 31) & (ald >> 31) | ((alt >> 31) ^ (ald >> 31)) & (~alo >> 31);
      Q   = C ^ SS;
      R1  = (alo == 0) ? 0 : (R1 & (Q ^ (SS ^ SD)));
      S   = alo >> 31;
      sfi = (sfi << 1) | Q;
      ald = (alo << 1) | (sfi >> 31);
      if ((alo >> 31) ^ (ald >> 31))
	{
	  DBZ = 1;
	}
    }

  /* Nth Loop */
  alt = Q ? ~als : als;
  alo = ald + alt + Q;
  C   = (alt >> 31) & (ald >> 31) | ((alt >> 31) ^ (ald >> 31)) & (~alo >> 31);
  Q   = C ^ SS;
  R1  = (alo == 0) ? 0 : (R1 & (Q ^ (SS ^ SD)));
  sfi = (sfi << (32-N+1));
  ald = alo;

  /* End */
  if (alo != 0)
    {
      alt = Q ? ~als : als;
      alo = ald + alt + Q;
    }
  R1  = R1 & ((~alo >> 31) ^ SD);
  if ((alo != 0) && ((Q ^ (SS ^ SD)) ^ R1)) alo = ald;
  if (N != 32)
    ald = sfi = (long) ((sfi >> 1) | (SS ^ SD) << 31) >> (32-N-1) | Q;
  else
    ald = sfi = sfi | Q;
  
  OV = DBZ | ((alo == 0) ? 0 : R1);
  
  * remainder_ptr = alo;

  /* Adj */
  if ((alo != 0) && ((SS ^ SD) ^ R1) || (alo == 0) && (SS ^ R1))
    alo = ald + 1;
  else
    alo = ald;
  
  OV  = (DBZ | R1) ? OV : ((alo >> 31) & (~ald >> 31));

  * quotient_ptr  = alo;
  * overflow_ptr  = OV;
}

/* sdivun imm5, reg1, reg2, reg3 */
int
OP_1C207E0 (void)
{
  unsigned long int  quotient;
  unsigned long int  remainder;
  unsigned long int  divide_by;
  unsigned long int  divide_this;
  boolean            overflow = false;
  unsigned int       imm5;
      
  trace_input ("sdivun", OP_IMM_REG_REG_REG, 0);

  imm5 = 32 - ((OP[3] & 0x3c0000) >> 17);

  divide_by   = State.regs[ OP[0] ];
  divide_this = State.regs[ OP[1] ] << imm5;

  divun (imm5, divide_by, divide_this, & quotient, & remainder, & overflow);
  
  State.regs[ OP[1]       ] = quotient;
  State.regs[ OP[2] >> 11 ] = remainder;
  
  /* Set condition codes.  */
  PSW &= ~(PSW_Z | PSW_S | PSW_OV);
  
  if (overflow)      PSW |= PSW_OV;
  if (quotient == 0) PSW |= PSW_Z;
  if (quotient & 0x80000000) PSW |= PSW_S;
  
  trace_output (OP_IMM_REG_REG_REG);

  return 4;
}

/* sdivn imm5, reg1, reg2, reg3 */
int
OP_1C007E0 (void)
{
  signed long int  quotient;
  signed long int  remainder;
  signed long int  divide_by;
  signed long int  divide_this;
  boolean          overflow = false;
  unsigned int     imm5;
      
  trace_input ("sdivn", OP_IMM_REG_REG_REG, 0);

  imm5 = 32 - ((OP[3] & 0x3c0000) >> 17);

  divide_by   = State.regs[ OP[0] ];
  divide_this = State.regs[ OP[1] ] << imm5;

  divn (imm5, divide_by, divide_this, & quotient, & remainder, & overflow);
  
  State.regs[ OP[1]       ] = quotient;
  State.regs[ OP[2] >> 11 ] = remainder;
  
  /* Set condition codes.  */
  PSW &= ~(PSW_Z | PSW_S | PSW_OV);
  
  if (overflow)      PSW |= PSW_OV;
  if (quotient == 0) PSW |= PSW_Z;
  if (quotient <  0) PSW |= PSW_S;
  
  trace_output (OP_IMM_REG_REG_REG);

  return 4;
}

/* sdivhun imm5, reg1, reg2, reg3 */
int
OP_18207E0 (void)
{
  unsigned long int  quotient;
  unsigned long int  remainder;
  unsigned long int  divide_by;
  unsigned long int  divide_this;
  boolean            overflow = false;
  unsigned int       imm5;
      
  trace_input ("sdivhun", OP_IMM_REG_REG_REG, 0);

  imm5 = 32 - ((OP[3] & 0x3c0000) >> 17);

  divide_by   = State.regs[ OP[0] ] & 0xffff;
  divide_this = State.regs[ OP[1] ] << imm5;

  divun (imm5, divide_by, divide_this, & quotient, & remainder, & overflow);
  
  State.regs[ OP[1]       ] = quotient;
  State.regs[ OP[2] >> 11 ] = remainder;
  
  /* Set condition codes.  */
  PSW &= ~(PSW_Z | PSW_S | PSW_OV);
  
  if (overflow)      PSW |= PSW_OV;
  if (quotient == 0) PSW |= PSW_Z;
  if (quotient & 0x80000000) PSW |= PSW_S;
  
  trace_output (OP_IMM_REG_REG_REG);

  return 4;
}

/* sdivhn imm5, reg1, reg2, reg3 */
int
OP_18007E0 (void)
{
  signed long int  quotient;
  signed long int  remainder;
  signed long int  divide_by;
  signed long int  divide_this;
  boolean          overflow = false;
  unsigned int     imm5;
      
  trace_input ("sdivhn", OP_IMM_REG_REG_REG, 0);

  imm5 = 32 - ((OP[3] & 0x3c0000) >> 17);

  divide_by   = SEXT16 (State.regs[ OP[0] ]);
  divide_this = State.regs[ OP[1] ] << imm5;

  divn (imm5, divide_by, divide_this, & quotient, & remainder, & overflow);
  
  State.regs[ OP[1]       ] = quotient;
  State.regs[ OP[2] >> 11 ] = remainder;
  
  /* Set condition codes.  */
  PSW &= ~(PSW_Z | PSW_S | PSW_OV);
  
  if (overflow)      PSW |= PSW_OV;
  if (quotient == 0) PSW |= PSW_Z;
  if (quotient <  0) PSW |= PSW_S;
  
  trace_output (OP_IMM_REG_REG_REG);

  return 4;
}
/* end-sanitize-v850eq */

/* start-sanitize-v850e */
/* divu  reg1, reg2, reg3 */
int
OP_2C207E0 (void)
{
  unsigned long int quotient;
  unsigned long int remainder;
  unsigned long int divide_by;
  unsigned long int divide_this;
  boolean           overflow = false;
  
  if ((OP[3] & 0x3c0000) == 0)
    {
      trace_input ("divu", OP_REG_REG_REG, 0);

      /* Compute the result.  */

      divide_by   = State.regs[ OP[0] ];
      divide_this = State.regs[ OP[1] ];

      if (divide_by == 0)
	{
	  overflow = true;
	  divide_by  = 1;
	}
	    
      State.regs[ OP[1]       ] = quotient  = divide_this / divide_by;
      State.regs[ OP[2] >> 11 ] = remainder = divide_this % divide_by;

      /* Set condition codes.  */
      PSW &= ~(PSW_Z | PSW_S | PSW_OV);
      
      if (overflow)      PSW |= PSW_OV;
      if (quotient == 0) PSW |= PSW_Z;
      if (quotient & 0x80000000) PSW |= PSW_S;

      trace_output (OP_REG_REG_REG);
    }
/* start-sanitize-v850eq */
/* divun imm5, reg1, reg2, reg3 */
  else
    {
      unsigned int imm5;
      
      trace_input ("divun", OP_IMM_REG_REG_REG, 0);

      imm5 = 32 - ((OP[3] & 0x3c0000) >> 17);

      divide_by   = State.regs[ OP[0] ];
      divide_this = State.regs[ OP[1] ];

      divun (imm5, divide_by, divide_this, & quotient, & remainder, & overflow);
      
      State.regs[ OP[1]       ] = quotient;
      State.regs[ OP[2] >> 11 ] = remainder;
      
      /* Set condition codes.  */
      PSW &= ~(PSW_Z | PSW_S | PSW_OV);
      
      if (overflow)      PSW |= PSW_OV;
      if (quotient == 0) PSW |= PSW_Z;
      if (quotient & 0x80000000) PSW |= PSW_S;

      trace_output (OP_IMM_REG_REG_REG);
    }
/* end-sanitize-v850eq */

  return 4;
}

/* div  reg1, reg2, reg3 */
int
OP_2C007E0 (void)
{
  signed long int quotient;
  signed long int remainder;
  signed long int divide_by;
  signed long int divide_this;
  boolean         overflow = false;
  
  if ((OP[3] & 0x3c0000) == 0)
    {
      trace_input ("div", OP_REG_REG_REG, 0);

      /* Compute the result.  */

      divide_by   = State.regs[ OP[0] ];
      divide_this = State.regs[ OP[1] ];

      if (divide_by == 0 || (divide_by == -1 && divide_this == (1 << 31)))
	{
	  overflow  = true;
	  divide_by = 1;
	}
	    
      State.regs[ OP[1]       ] = quotient  = divide_this / divide_by;
      State.regs[ OP[2] >> 11 ] = remainder = divide_this % divide_by;

      /* Set condition codes.  */
      PSW &= ~(PSW_Z | PSW_S | PSW_OV);
      
      if (overflow)      PSW |= PSW_OV;
      if (quotient == 0) PSW |= PSW_Z;
      if (quotient <  0) PSW |= PSW_S;

      trace_output (OP_REG_REG_REG);
    }
/* start-sanitize-v850eq */
/* divn imm5, reg1, reg2, reg3 */
  else
    {
      unsigned int imm5;
      
      trace_input ("divn", OP_IMM_REG_REG_REG, 0);

      imm5 = 32 - ((OP[3] & 0x3c0000) >> 17);

      divide_by   = State.regs[ OP[0] ];
      divide_this = State.regs[ OP[1] ];

      divn (imm5, divide_by, divide_this, & quotient, & remainder, & overflow);
      
      State.regs[ OP[1]       ] = quotient;
      State.regs[ OP[2] >> 11 ] = remainder;
      
      /* Set condition codes.  */
      PSW &= ~(PSW_Z | PSW_S | PSW_OV);
      
      if (overflow)      PSW |= PSW_OV;
      if (quotient == 0) PSW |= PSW_Z;
      if (quotient <  0) PSW |= PSW_S;

      trace_output (OP_IMM_REG_REG_REG);
    }
/* end-sanitize-v850eq */

  return 4;
}

/* divhu  reg1, reg2, reg3 */
int
OP_28207E0 (void)
{
  unsigned long int quotient;
  unsigned long int remainder;
  unsigned long int divide_by;
  unsigned long int divide_this;
  boolean           overflow = false;
  
  if ((OP[3] & 0x3c0000) == 0)
    {
      trace_input ("divhu", OP_REG_REG_REG, 0);

      /* Compute the result.  */

      divide_by   = State.regs[ OP[0] ] & 0xffff;
      divide_this = State.regs[ OP[1] ];

      if (divide_by == 0)
	{
	  overflow = true;
	  divide_by  = 1;
	}
	    
      State.regs[ OP[1]       ] = quotient  = divide_this / divide_by;
      State.regs[ OP[2] >> 11 ] = remainder = divide_this % divide_by;

      /* Set condition codes.  */
      PSW &= ~(PSW_Z | PSW_S | PSW_OV);
      
      if (overflow)      PSW |= PSW_OV;
      if (quotient == 0) PSW |= PSW_Z;
      if (quotient & 0x80000000) PSW |= PSW_S;

      trace_output (OP_REG_REG_REG);
    }
/* start-sanitize-v850eq */
/* divhun imm5, reg1, reg2, reg3 */
  else
    {
      unsigned int imm5;
      
      trace_input ("divhun", OP_IMM_REG_REG_REG, 0);

      imm5 = 32 - ((OP[3] & 0x3c0000) >> 17);

      divide_by   = State.regs[ OP[0] ] & 0xffff;
      divide_this = State.regs[ OP[1] ];

      divun (imm5, divide_by, divide_this, & quotient, & remainder, & overflow);
      
      State.regs[ OP[1]       ] = quotient;
      State.regs[ OP[2] >> 11 ] = remainder;
      
      /* Set condition codes.  */
      PSW &= ~(PSW_Z | PSW_S | PSW_OV);
      
      if (overflow)      PSW |= PSW_OV;
      if (quotient == 0) PSW |= PSW_Z;
      if (quotient & 0x80000000) PSW |= PSW_S;

      trace_output (OP_IMM_REG_REG_REG);
    }
/* end-sanitize-v850eq */

  return 4;
}

/* divh  reg1, reg2, reg3 */
int
OP_28007E0 (void)
{
  signed long int quotient;
  signed long int remainder;
  signed long int divide_by;
  signed long int divide_this;
  boolean         overflow = false;
  
  if ((OP[3] & 0x3c0000) == 0)
    {
      trace_input ("divh", OP_REG_REG_REG, 0);

      /* Compute the result.  */

      divide_by  = State.regs[ OP[0] ];
      divide_this = SEXT16 (State.regs[ OP[1] ]);

      if (divide_by == 0 || (divide_by == -1 && divide_this == (1 << 31)))
	{
	  overflow = true;
	  divide_by  = 1;
	}
	    
      State.regs[ OP[1]       ] = quotient  = divide_this / divide_by;
      State.regs[ OP[2] >> 11 ] = remainder = divide_this % divide_by;

      /* Set condition codes.  */
      PSW &= ~(PSW_Z | PSW_S | PSW_OV);
      
      if (overflow)      PSW |= PSW_OV;
      if (quotient == 0) PSW |= PSW_Z;
      if (quotient <  0) PSW |= PSW_S;

      trace_output (OP_REG_REG_REG);
    }
/* start-sanitize-v850eq */
/* divhn imm5, reg1, reg2, reg3 */
  else
    {
      unsigned int imm5;
      
      trace_input ("divhn", OP_IMM_REG_REG_REG, 0);

      imm5 = 32 - ((OP[3] & 0x3c0000) >> 17);

      divide_by   = SEXT16 (State.regs[ OP[0] ]);
      divide_this = State.regs[ OP[1] ];

      divn (imm5, divide_by, divide_this, & quotient, & remainder, & overflow);
      
      State.regs[ OP[1]       ] = quotient;
      State.regs[ OP[2] >> 11 ] = remainder;
      
      /* Set condition codes.  */
      PSW &= ~(PSW_Z | PSW_S | PSW_OV);
      
      if (overflow)      PSW |= PSW_OV;
      if (quotient == 0) PSW |= PSW_Z;
      if (quotient <  0) PSW |= PSW_S;

      trace_output (OP_IMM_REG_REG_REG);
    }
/* end-sanitize-v850eq */

  return 4;
}

/* mulu imm9, reg2, reg3 */
int
OP_24207E0 (void)
{
  trace_input ("mulu", OP_IMM_REG_REG, 0);

  Multiply64 (false, (OP[3] & 0x1f) | ((OP[3] >> 13) & 0x1e0));

  trace_output (OP_IMM_REG_REG);

  return 4;
}

/* mul imm9, reg2, reg3 */
int
OP_24007E0 (void)
{
  trace_input ("mul", OP_IMM_REG_REG, 0);

  Multiply64 (true, (OP[3] & 0x1f) | ((OP[3] >> 13) & 0x1e0));

  trace_output (OP_IMM_REG_REG);

  return 4;
}

/* cmov imm5, reg2, reg3 */
int
OP_30007E0 (void)
{
  trace_input ("cmov", OP_IMM_REG_REG, 0);

  State.regs[ OP[2] >> 11 ] = condition_met (OP[0]) ? SEXT5( OP[0] ) : State.regs[ OP[1] ];
  
  trace_output (OP_IMM_REG_REG);

  return 4;
  
}

/* ctret */
int
OP_14407E0 (void)
{
  trace_input ("ctret", OP_NONE, 0);

  PC  = CTPC;
  PSW = CTPSW;

  trace_output (OP_NONE);

  return 0;
}

/* hsw */
int
OP_34407E0 (void)
{
  unsigned long value;
  
  trace_input ("hsw", OP_REG_REG3, 0);

  value = State.regs[ OP[ 1 ] ];
  value >>= 16;
  value |= (State.regs[ OP[ 1 ] ] << 16);
  
  State.regs[ OP[2] >> 11 ] = value;

  PSW &= ~(PSW_Z | PSW_S | PSW_CY | PSW_OV);

  if (value == 0) PSW |= PSW_Z;
  if (value & 0x80000000) PSW |= PSW_S;
  if (((value & 0xffff) == 0) || (value & 0xffff0000) == 0) PSW |= PSW_CY;

  trace_output (OP_REG_REG3);
  
  return 4;
}

#define WORDHASNULLBYTE(x) (((x) - 0x01010101) & ~(x)&0x80808080)

/* bsw */
int
OP_34007E0 (void)
{
  unsigned long value;
  
  trace_input ("bsw", OP_REG_REG3, 0);

  value = State.regs[ OP[ 1 ] ];
  value >>= 24;
  value |= (State.regs[ OP[ 1 ] ] << 24);
  value |= ((State.regs[ OP[ 1 ] ] << 8) & 0x00ff0000);
  value |= ((State.regs[ OP[ 1 ] ] >> 8) & 0x0000ff00);
  
  State.regs[ OP[2] >> 11 ] = value;

  PSW &= ~(PSW_Z | PSW_S | PSW_CY | PSW_OV);

  if (value == 0) PSW |= PSW_Z;
  if (value & 0x80000000) PSW |= PSW_S;
  if (WORDHASNULLBYTE (value)) PSW |= PSW_CY;

  trace_output (OP_REG_REG3);
  
  return 4;
}

/* bsh */
int
OP_34207E0 (void)
{
  unsigned long value;
  
  trace_input ("bsh", OP_REG_REG3, 0);

  value   = State.regs[ OP[ 1 ] ];
  value >>= 8;
  value  |= ((State.regs[ OP[ 1 ] ] << 8) & 0xff00ff00);
  value  |= ((State.regs[ OP[ 1 ] ] >> 8) & 0x000000ff);
  
  State.regs[ OP[2] >> 11 ] = value;

  PSW &= ~(PSW_Z | PSW_S | PSW_CY | PSW_OV);

  if (value == 0) PSW |= PSW_Z;
  if (value & 0x80000000) PSW |= PSW_S;
  if (((value & 0xff) == 0) || (value & 0x00ff) == 0) PSW |= PSW_CY;

  trace_output (OP_REG_REG3);
  
  return 4;
}

/* pushml list18 */
/* ld.hu */
int
OP_107E0 (void)
{
  if (OP[ 1 ] == 0)
    {
      int i;

      trace_input ("pushml", OP_PUSHPOP3, 0);

      /* Store the registers with lower number registers being placed at higher addresses.  */
      for (i = 0; i < 15; i++)
	if ((OP[3] & (1 << type3_regs[ i ])))
	  {
	    SP -= 4;
	    store_mem (SP & ~ 3, 4, State.regs[ i + 1 ]);
	  }

      if (OP[3] & (1 << 3))
	{
	  SP -= 4;

	  store_mem (SP & ~ 3, 4, PSW);
	}
	  
      if (OP[3] & (1 << 19))
	{
	  SP -= 8;

	  if ((PSW & PSW_NP) && ((PSW & PSW_EP) == 0))
	    {
	      store_mem ((SP + 4) & ~ 3, 4, FEPC);
	      store_mem ( SP      & ~ 3, 4, FEPSW);
	    }
	  else
	    {
	      store_mem ((SP + 4) & ~ 3, 4, EIPC);
	      store_mem ( SP      & ~ 3, 4, EIPSW);
	    }
	}

      trace_output (OP_PUSHPOP2);
    }
  else
    {
      int adr;

      trace_input ("ld.hu", OP_LOAD32, 2);

      adr = State.regs[ OP[0] ] + SEXT16 (OP[2] & ~1);
      adr &= ~0x1;
      
      State.regs[ OP[1] ] = load_mem (adr, 2);
      
      trace_output (OP_LOAD32);
    }
  
  return 4;
}

/* prepare list12, imm5 */
/* ld.bu */
int
OP_10780 (void)
{
  if (OP[ 1 ] == 0)
    {
      int  i;
      
      trace_input ("prepare", OP_PUSHPOP1, 0);
      
      /* Store the registers with lower number registers being placed at higher addresses.  */
      for (i = 0; i < 12; i++)
	if ((OP[3] & (1 << type1_regs[ i ])))
	  {
	    SP -= 4;
	    store_mem (SP, 4, State.regs[ 20 + i ]);
	  }

      SP -= (OP[3] & 0x3e) << 1;

      trace_output (OP_PUSHPOP1);
    }
  else
    {
      int adr;

      trace_input ("ld.bu", OP_LOAD32, 1);

      adr = State.regs[ OP[0] ] + SEXT16 (OP[2] & ~1) | ((OP[3] >> 5) & 1);
      
      State.regs[ OP[1] ] = load_mem (adr, 1);
  
      trace_output (OP_LOAD32);
    }
  
  return 4;
}

/* prepare list12, imm5, imm32 */
int
OP_1B0780 (void)
{
  int  i;
  
  trace_input ("prepare", OP_PUSHPOP1, 0);
  
  /* Store the registers with lower number registers being placed at higher addresses.  */
  for (i = 0; i < 12; i++)
    if ((OP[3] & (1 << type1_regs[ i ])))
      {
	SP -= 4;
	store_mem (SP, 4, State.regs[ 20 + i ]);
      }
  
  SP -= (OP[3] & 0x3e) << 1;

  EP = load_mem (PC + 4, 4);
  
  trace_output (OP_PUSHPOP1);

  return 8;
}

/* prepare list12, imm5, imm16-32 */
int
OP_130780 (void)
{
  int  i;
  
  trace_input ("prepare", OP_PUSHPOP1, 0);
  
  /* Store the registers with lower number registers being placed at higher addresses.  */
  for (i = 0; i < 12; i++)
    if ((OP[3] & (1 << type1_regs[ i ])))
      {
	SP -= 4;
	store_mem (SP, 4, State.regs[ 20 + i ]);
      }
  
  SP -= (OP[3] & 0x3e) << 1;

  EP = load_mem (PC + 4, 2) << 16;
  
  trace_output (OP_PUSHPOP1);

  return 6;
}

/* prepare list12, imm5, imm16 */
int
OP_B0780 (void)
{
  int  i;
  
  trace_input ("prepare", OP_PUSHPOP1, 0);
  
  /* Store the registers with lower number registers being placed at higher addresses.  */
  for (i = 0; i < 12; i++)
    if ((OP[3] & (1 << type1_regs[ i ])))
      {
	SP -= 4;
	store_mem (SP, 4, State.regs[ 20 + i ]);
      }
  
  SP -= (OP[3] & 0x3e) << 1;

  EP = SEXT16 (load_mem (PC + 4, 2));
  
  trace_output (OP_PUSHPOP1);

  return 6;
}

/* prepare list12, imm5, sp */
int
OP_30780 (void)
{
  int  i;
  
  trace_input ("prepare", OP_PUSHPOP1, 0);
  
  /* Store the registers with lower number registers being placed at higher addresses.  */
  for (i = 0; i < 12; i++)
    if ((OP[3] & (1 << type1_regs[ i ])))
      {
	SP -= 4;
	store_mem (SP, 4, State.regs[ 20 + i ]);
      }
  
  SP -= (OP[3] & 0x3e) << 1;

  EP = SP;
  
  trace_output (OP_PUSHPOP1);

  return 4;
}

/* sld.hu */
int
OP_70 (void)
{
  unsigned long result;
  
  result  = load_mem (State.regs[30] + ((OP[3] & 0xf) << 1), 2);

/* start-sanitize-v850eq */
#ifdef ARCH_v850eq
  trace_input ("sld.h", OP_LOAD16, 2);
  
  State.regs[ OP[1] ] = SEXT16 (result);
#else
/* end-sanitize-v850eq */
  trace_input ("sld.hu", OP_LOAD16, 2);
  
  State.regs[ OP[1] ] = result;
/* start-sanitize-v850eq */
#endif
/* end-sanitize-v850eq */
  
  trace_output (OP_LOAD16);
  
  return 2;
}

/* cmov reg1, reg2, reg3 */
int
OP_32007E0 (void)
{
  trace_input ("cmov", OP_REG_REG_REG, 0);

  State.regs[ OP[2] >> 11 ] = condition_met (OP[0]) ? State.regs[ OP[0] ] : State.regs[ OP[1] ];
  
  trace_output (OP_REG_REG_REG);

  return 4;
}

/* mul reg1, reg2, reg3 */
int
OP_22007E0 (void)
{
  trace_input ("mul", OP_REG_REG_REG, 0);

  Multiply64 (true, State.regs[ OP[0] ]);

  trace_output (OP_REG_REG_REG);

  return 4;
}

/* end-sanitize-v850e */
/* start-sanitize-v850eq */

/* popmh list18 */
int
OP_307F0 (void)
{
  int i;
  
  trace_input ("popmh", OP_PUSHPOP2, 0);
  
  if (OP[3] & (1 << 19))
    {
      if ((PSW & PSW_NP) && ((PSW & PSW_EP) == 0))
	{
	  FEPSW = load_mem ( SP      & ~ 3, 4);
	  FEPC  = load_mem ((SP + 4) & ~ 3, 4);
	}
      else
	{
	  EIPSW = load_mem ( SP      & ~ 3, 4);
	  EIPC  = load_mem ((SP + 4) & ~ 3, 4);
	}
      
      SP += 8;
    }
  
  /* Load the registers with lower number registers being retrieved from higher addresses.  */
  for (i = 16; i--;)
    if ((OP[3] & (1 << type2_regs[ i ])))
      {
	State.regs[ i + 16 ] = load_mem (SP & ~ 3, 4);
	SP += 4;
      }
  
  trace_output (OP_PUSHPOP2);

  return 4;
}

/* popml lsit18 */
int
OP_107F0 (void)
{
  int i;

  trace_input ("popml", OP_PUSHPOP3, 0);

  if (OP[3] & (1 << 19))
    {
      if ((PSW & PSW_NP) && ((PSW & PSW_EP) == 0))
	{
	  FEPSW = load_mem ( SP      & ~ 3, 4);
	  FEPC =  load_mem ((SP + 4) & ~ 3, 4);
	}
      else
	{
	  EIPSW = load_mem ( SP      & ~ 3, 4);
	  EIPC  = load_mem ((SP + 4) & ~ 3, 4);
	}
      
      SP += 8;
    }
  
  if (OP[3] & (1 << 3))
    {
      PSW = load_mem (SP & ~ 3, 4);
      SP += 4;
    }
  
  /* Load the registers with lower number registers being retrieved from higher addresses.  */
  for (i = 15; i--;)
    if ((OP[3] & (1 << type3_regs[ i ])))
      {
	State.regs[ i + 1 ] = load_mem (SP & ~ 3, 4);
	SP += 4;
      }
  
  trace_output (OP_PUSHPOP2);
}

/* pushmh list18 */
int
OP_307E0 (void)
{
  int i;

  trace_input ("pushmh", OP_PUSHPOP2, 0);
  
  /* Store the registers with lower number registers being placed at higher addresses.  */
  for (i = 0; i < 16; i++)
    if ((OP[3] & (1 << type2_regs[ i ])))
      {
	SP -= 4;
	store_mem (SP & ~ 3, 4, State.regs[ i + 16 ]);
      }
  
  if (OP[3] & (1 << 19))
    {
      SP -= 8;
      
      if ((PSW & PSW_NP) && ((PSW & PSW_EP) == 0))
	{
	  store_mem ((SP + 4) & ~ 3, 4, FEPC);
	  store_mem ( SP      & ~ 3, 4, FEPSW);
	}
      else
	{
	  store_mem ((SP + 4) & ~ 3, 4, EIPC);
	  store_mem ( SP      & ~ 3, 4, EIPSW);
	}
    }
  
  trace_output (OP_PUSHPOP2);

  return 4;
}

/* end-sanitize-v850eq */
