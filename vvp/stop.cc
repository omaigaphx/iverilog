/*
 * Copyright (c) 2003 Stephen Williams (steve@icarus.com)
 *
 *    This source code is free software; you can redistribute it
 *    and/or modify it in source code form under the terms of the GNU
 *    General Public License as published by the Free Software
 *    Foundation; either version 2 of the License, or (at your option)
 *    any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 */
#ifdef HAVE_CVS_IDENT
#ident "$Id: stop.cc,v 1.12 2004/10/04 01:10:59 steve Exp $"
#endif

/*
 * This file provides a simple command line debugger for the vvp
 * runtime. It is a means to interact with the user running the
 * simulation.
 */

# include  "config.h"


# include  "vpi_priv.h"
# include  "vthread.h"
# include  "schedule.h"
# include  <stdio.h>
# include  <ctype.h>
#ifdef HAVE_READLINE_READLINE_H
# include  <readline/readline.h>
#endif
#ifdef HAVE_READLINE_HISTORY_H
# include  <readline/history.h>
#endif
# include  <string.h>
# include  <stdlib.h>
#ifdef HAVE_MALLOC_H
# include  <malloc.h>
#endif

struct __vpiScope*stop_current_scope = 0;

#ifdef USE_READLINE

static bool interact_flag = true;

static void cmd_call(unsigned argc, char*argv[])
{
      struct __vpiHandle**table;
      unsigned ntable;

      if (stop_current_scope == 0) {
	    vpip_make_root_iterator(table, ntable);

      } else {
	    table = stop_current_scope->intern;
	    ntable = stop_current_scope->nintern;
      }

	/* This is an array of vpiHandles, for passing to the created
	   command. */
      unsigned vpi_argc = argc - 1;
      vpiHandle*vpi_argv = (vpiHandle*)calloc(vpi_argc, sizeof(vpiHandle));
      vpiHandle*vpi_free = (vpiHandle*)calloc(vpi_argc, sizeof(vpiHandle));

      unsigned errors = 0;

      for (unsigned idx = 0 ;  idx < vpi_argc ;  idx += 1) {
	    vpiHandle handle = 0;
	    bool add_to_free_list = false;

	      /* Detect the special case that the argument is the
		 .(dot) string. This represents the handle for the
		 current scope. */
	    if (stop_current_scope && (strcmp(argv[idx+1], ".") == 0))
		  handle = &stop_current_scope->base;

	      /* Is the argument a quoted string? */
	    if (handle == 0 && argv[idx+1][0] == '"') {
		  char*tmp = strdup(argv[idx+1]);
		  tmp[strlen(tmp)-1] = 0;

		    /* Create a temporary vpiStringConst to pass as a
		       handle. Make it temporary so that memory is
		       reclaimed after the call is completed. */
		  handle = vpip_make_string_const(strdup(tmp+1), false);
		  add_to_free_list = true;
		  free(tmp);
	    }

	      /* Is the argument a decimal constant? */
	    if (handle == 0
		&& strspn(argv[idx+1],"0123456789") == strlen(argv[idx+1])) {
		  handle = vpip_make_dec_const(strtol(argv[idx+1],0,10));
		  add_to_free_list = true;
	    }

	      /* Try to find the vpiHandle within this scope that has
		 the name in argv[idx+2]. Look in the current scope. */

	    for (unsigned tmp = 0 ;  (tmp < ntable)&& !handle ;  tmp += 1) {
		  struct __vpiScope*scope;
		  const char*name;

		  switch (table[tmp]->vpi_type->type_code) {

		      case vpiModule:
		      case vpiFunction:
		      case vpiTask:
		      case vpiNamedBegin:
		      case vpiNamedFork:
			scope = (struct __vpiScope*) table[idx];
			if (strcmp(scope->name, argv[idx+1]) == 0)
			      handle = table[tmp];
			break;

		      case vpiReg:
		      case vpiNet:
		      case vpiParameter:
			name = vpi_get_str(vpiName,table[tmp]);
			if (strcmp(argv[idx+1], name) == 0)
			      handle = table[tmp];
			break;
		  }
	    }

	    if (handle == 0) {
		  printf("call error: I don't know how to"
			 " pass %s to %s\n", argv[idx+1], argv[0]);
		  errors += 1;
	    }

	    vpi_argv[idx] = handle;
	    if (add_to_free_list)
		  vpi_free[idx] = handle;
	    else
		  vpi_free[idx] = 0;

      }

	/* If there are no errors so far, then make up a call to the
	   vpi task and execute that call. Free the call structure
	   when we finish. */
      if (errors == 0) {
	    vpiHandle call_handle = vpip_build_vpi_call(argv[0], 0, 0,
							vpi_argc, vpi_argv);
	    if (call_handle == 0)
		  goto out;

	    vpip_execute_vpi_call(0, call_handle);
	    vpi_free_object(call_handle);
      }

 out:
      for (unsigned idx = 0 ;  idx < vpi_argc ;  idx += 1) {
	    if (vpi_free[idx])
		  vpi_free_object(vpi_free[idx]);
      }

      free(vpi_argv);
      free(vpi_free);
}

static void cmd_cont(unsigned, char*[])
{
      interact_flag = false;
}

static void cmd_finish(unsigned, char*[])
{
      interact_flag = false;
      schedule_finish(0);
}

static void cmd_help(unsigned, char*[]);

static void cmd_list(unsigned, char*[])
{
      struct __vpiHandle**table;
      unsigned ntable;

      if (stop_current_scope == 0) {
	    vpip_make_root_iterator(table, ntable);

      } else {
	    table = stop_current_scope->intern;
	    ntable = stop_current_scope->nintern;
      }

      printf("%u items in this scope:\n", ntable);
      for (unsigned idx = 0 ;  idx < ntable ;  idx += 1) {

	    struct __vpiScope*scope;
	    struct __vpiSignal*sig;

	    switch (table[idx]->vpi_type->type_code) {
		case vpiModule:
		  scope = (struct __vpiScope*) table[idx];
		  printf("module  : %s\n", scope->name);
		  break;

		case vpiTask:
		  scope = (struct __vpiScope*) table[idx];
		  printf("task    : %s\n", scope->name);
		  break;

		case vpiFunction:
		  scope = (struct __vpiScope*) table[idx];
		  printf("function: %s\n", scope->name);
		  break;

		case vpiNamedBegin:
		  scope = (struct __vpiScope*) table[idx];
		  printf("block   : %s\n", scope->name);
		  break;

		case vpiNamedFork:
		  scope = (struct __vpiScope*) table[idx];
		  printf("fork    : %s\n", scope->name);
		  break;

		case vpiParameter:
		  printf("param   : %s\n", vpi_get_str(vpiName, table[idx]));
		  break;

		case vpiReg:
		  sig = (struct __vpiSignal*) table[idx];
		  if ((sig->msb == 0) && (sig->lsb == 0))
			printf("reg     : %s%s\n", sig->name,
			       sig->signed_flag? "signed " : "");
		  else
			printf("reg     : %s%s[%d:%d]\n", sig->name,
			       sig->signed_flag? "signed " : "",
			       sig->msb, sig->lsb);
		  break;

		case vpiNet:
		  sig = (struct __vpiSignal*) table[idx];
		  if ((sig->msb == 0) && (sig->lsb == 0))
			printf("net     : %s%s\n", sig->name,
			       sig->signed_flag? "signed " : "");
		  else
			printf("net     : %s%s[%d:%d]\n", sig->name,
			       sig->signed_flag? "signed " : "",
			       sig->msb, sig->lsb);
		  break;

		default:
		  printf("%8d: <vpi handle>\n",
			 table[idx]->vpi_type->type_code);
		  break;
	    }

      }
}

static void cmd_load(unsigned argc, char*argv[])
{
      unsigned idx;

      for (idx = 1 ;  idx < argc ;  idx += 1) {
	    printf("Loading module %s...\n", argv[idx]);
	    vpip_load_module(argv[idx]);
      }
}

static void cmd_pop(unsigned, char*[])
{
      if (stop_current_scope != 0)
	    stop_current_scope = stop_current_scope->scope;
}

static void cmd_push(unsigned argc, char* argv[])
{

      for (unsigned idx = 1 ;  idx < argc ;  idx += 1) {
	    struct __vpiHandle**table;
	    unsigned ntable;

	    struct __vpiScope*child = 0;

	    if (stop_current_scope) {
		  table = stop_current_scope->intern;
		  ntable = stop_current_scope->nintern;
	    } else {
		  vpip_make_root_iterator(table, ntable);
	    }

	    child = 0;
	    unsigned tmp;
	    for (tmp = 0 ;  tmp < ntable ;  tmp += 1) {
		  if (table[tmp]->vpi_type->type_code != vpiModule)
			continue;

		  struct __vpiScope*cp = (struct __vpiScope*) table[tmp];

		    /* This is a scope, and the name matches, then
		       report that I found the child. */
		  if (strcmp(cp->name, argv[idx]) == 0) {
			child = cp;
			break;
		  }
	    }

	    if (child == 0) {
		  printf("Scope %s not found.\n", argv[idx]);
		  return;
	    }

	    stop_current_scope = child;
      }
}

static void cmd_time(unsigned, char*[])
{
      unsigned long ticks = schedule_simtime();
      printf("%lu ticks\n", ticks);
}

static void cmd_where(unsigned, char*[])
{
      struct __vpiScope*cur = stop_current_scope;

      while (cur) {
	    switch (cur->base.vpi_type->type_code) {
		case vpiModule:
		  printf("module %s\n",
			 cur->name);
		  break;
		default:
		  printf("scope (%d) %s;\n",
			 cur->base.vpi_type->type_code,
			 cur->name);
		  break;
	    }

	    cur = cur->scope;
      }
}

static void cmd_unknown(unsigned, char*argv[])
{
      printf("Unknown command: %s\n", argv[0]);
      printf("Try the help command to get a summary\n"
	     "of available commands.\n");
}

struct cmd_table {
      const char*name;
      void (*proc)(unsigned argc, char*argv[]);
      const char*summary;
} cmd_table[] = {
      { "cd",     &cmd_push,
        "Synonym for push."},
      { "cont",   &cmd_cont,
        "Resume (continue) the simulation"},
      { "finish", &cmd_finish,
        "Finish the simulation."},
      { "help",   &cmd_help,
        "Get help."},
      { "list",   &cmd_list,
        "List items in the current scope."},
      { "load",   &cmd_load,
        "Load a VPI module, a la vvp -m."},
      { "ls",     &cmd_list,
        "Shorthand for \"list\"."},
      { "pop",    &cmd_pop,
        "Pop one scope from the scope stack."},
      { "push",   &cmd_push,
        "Descend into the named scope."},
      { "time",   &cmd_time,
        "Print the current simulation time."},
      { "where",  &cmd_where,
        "Show current scope, and scope hierarchy stack."},
      { 0,        &cmd_unknown, 0}
};

static void cmd_help(unsigned argc, char*argv[])
{
      printf("Commands can be from the following table of base commands,\n"
	     "or can be invocations of system tasks/functions.\n\n");
      for (unsigned idx = 0 ;  cmd_table[idx].name != 0 ;  idx += 1) {
	    printf("%-8s - %s\n", cmd_table[idx].name, cmd_table[idx].summary);
      }

      printf("\nIf the command name starts with a '$' character, it\n"
	     "is taken to be the name of a system task, and a call is\n"
	     "built up and executed.\n");
}


static void invoke_command(char*txt)
{
      unsigned argc = 0;
      char**argv = new char*[strlen(txt)/2];

	/* Chop the line into words. */
      for (char*cp = txt+strspn(txt, " ")
		 ; *cp; cp += strspn(cp, " ")) {
	    argv[argc] = cp;

	    if (cp[0] == '"') {
		  char*tmp = strchr(cp+1, '"');
		  if (tmp == 0) {
			printf("Missing close-quote: %s\n", cp);
			delete[]argv;
			return;
		  }

		  cp = tmp + 1;

	    } else {
		  cp += strcspn(cp, " ");
	    }

	    if (*cp) *cp++ = 0;

	    argc += 1;
      }


	/* Look up the command, using argv[0] as the key. */
      if (argc > 0) {

	    if (argv[0][0] == '$') {
		  cmd_call(argc, argv);

	    } else {
		  unsigned idx;
		  for (idx = 0 ;  cmd_table[idx].name ;  idx += 1)
			if (strcmp(cmd_table[idx].name, argv[0]) == 0)
			      break;

		  cmd_table[idx].proc (argc, argv);
	    }

      }

      delete[]argv;
}

void stop_handler(int rc)
{
      printf("** VVP Stop(%d) **\n", rc);
      printf("** Current simulation time is %" TIME_FMT "u ticks.\n",
      		schedule_simtime());

      interact_flag = true;
      while (interact_flag) {
	    char*input = readline("> ");
	    if (input == 0)
		  break;


	      /* Advance to the first input character. */
	    char*first = input;
	    while (*first && isspace(*first))
		  first += 1;

	    if (first[0] != 0) {
#ifdef HAVE_READLINE_HISTORY_H
		  add_history(first);
#endif
		  invoke_command(first);
	    }

	    free(input);
      }

      printf("** Continue **\n");
}

#else

void stop_handler(int rc)
{
      printf("** VVP Stop(%d) **\n", rc);
      printf("** Current simulation time is %" TIME_FMT "u ticks.\n",
      		schedule_simtime());

      printf("** Interactive mode not supported, exiting simulation.\n");
      exit(0);
}

#endif

/*
 * $Log: stop.cc,v $
 * Revision 1.12  2004/10/04 01:10:59  steve
 *  Clean up spurious trailing white space.
 *
 * Revision 1.11  2004/02/21 00:44:34  steve
 *  Add load command to interactive stop.
 *  Support decimal constants passed interactive to system tasks.
 *
 * Revision 1.10  2003/11/07 05:58:02  steve
 *  Fix conditional compilation of readline history.
 *
 * Revision 1.9  2003/10/15 02:17:39  steve
 *  Include net objects in list display.
 *
 * Revision 1.8  2003/05/16 03:50:28  steve
 *  Fallback functionality if readline is not present.
 *
 * Revision 1.7  2003/03/13 20:31:40  steve
 *  Warnings about long long time.
 *
 * Revision 1.6  2003/03/10 23:37:07  steve
 *  Direct support for string parameters.
 *
 * Revision 1.5  2003/03/08 20:59:41  steve
 *  Missing include ctype.h.
 *
 * Revision 1.4  2003/02/24 06:35:45  steve
 *  Interactive task calls take string arguments.
 *
 * Revision 1.3  2003/02/23 06:41:54  steve
 *  Add to interactive stop mode support for
 *  current scope, the ability to scan/traverse
 *  scopes, and the ability to call system tasks.
 *
 * Revision 1.2  2003/02/22 06:32:36  steve
 *  Basic support for system task calls.
 *
 * Revision 1.1  2003/02/21 03:40:35  steve
 *  Add vpiStop and interactive mode.
 *
 */

