/*
 * Copyright (c) 2005 William Pitcock, et al.
 * Rights to this code are as documented in doc/LICENSE.
 *
 * This file contains code for the nickserv DROP function.
 */

#include "atheme.h"

static void
cmd_ns_drop_func(struct sourceinfo *const restrict si, const int ATHEME_VATTR_UNUSED parc, char *parv[])
{
	const char *const acc = parv[0];
	const char *const pass = parv[1];
	const char *const key = parv[2];

	if (! acc || ! pass)
	{
		(void) command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "DROP");
		(void) command_fail(si, fault_needmoreparams, _("Syntax: DROP <account> <password>"));
		return;
	}

	myuser_t *mu;

	if (! (mu = myuser_find(acc)))
	{
		if (! nicksvs.no_nick_ownership)
		{
			mynick_t *const mn = mynick_find(acc);

			if (mn && command_find(si->service->commands, "UNGROUP"))
			{
				(void) command_fail(si, fault_nosuch_target, _("\2%s\2 is a grouped nick, use %s "
				                                               "to remove it."), acc, "UNGROUP");
				return;
			}
		}

		(void) command_fail(si, fault_nosuch_target, _("\2%s\2 is not registered."), acc);
		return;
	}

	if (metadata_find(mu, "private:freeze:freezer"))
	{
		(void) command_fail(si, fault_authfail, nicksvs.no_nick_ownership ?
		                    _("You cannot login as \2%s\2 because the account has been frozen.") :
		                    _("You cannot identify to \2%s\2 because the nickname has been frozen."),
		                    entity(mu)->name);
		return;
	}

	if (! verify_password(mu, pass))
	{
		(void) command_fail(si, fault_authfail, _("Authentication failed. Invalid password for \2%s\2."),
		                    entity(mu)->name);

		(void) bad_password(si, mu);
		return;
	}

	if (! nicksvs.no_nick_ownership && MOWGLI_LIST_LENGTH(&mu->nicks) > 1 &&
	    command_find(si->service->commands, "UNGROUP"))
	{
		(void) command_fail(si, fault_noprivs, _("Account \2%s\2 has %zu other nick(s) grouped to it, "
		                                         "remove those first."), entity(mu)->name,
		                                         MOWGLI_LIST_LENGTH(&mu->nicks) - 1);
		return;
	}

	if (is_soper(mu))
	{
		(void) command_fail(si, fault_noprivs, _("The nickname \2%s\2 belongs to a services operator; "
		                                         "it cannot be dropped."), acc);
		return;
	}

	if (mu->flags & MU_HOLD)
	{
		(void) command_fail(si, fault_noprivs, _("The account \2%s\2 is held; it cannot be dropped."), acc);
		return;
	}

	const char *const challenge = create_weak_challenge(si, entity(mu)->name);

	if (! challenge)
	{
		(void) command_fail(si, fault_internalerror, _("Failed to create challenge"));
		return;
	}

	if (! key)
	{
		char fullcmd[BUFSIZE];

		(void) snprintf(fullcmd, sizeof fullcmd, "/%s%s DROP %s %s %s", (ircd->uses_rcommand == false) ?
		                "msg " : "", nicksvs.me->disp, entity(mu)->name, pass, challenge);

		(void) command_success_nodata(si, _("This is a friendly reminder that you are about to \2destroy\2 "
		                                    "the account \2%s\2."), entity(mu)->name);

		(void) command_success_nodata(si, _("To avoid accidental use of this command, this operation has to "
		                                    "be confirmed. Please confirm by replying with \2%s\2"), fullcmd);
		return;
	}

	if (strcmp(challenge, key) != 0)
	{
		(void) command_fail(si, fault_badparams, _("Invalid key for %s."), "DROP");
		return;
	}

	(void) command_add_flood(si, FLOOD_MODERATE);
	(void) logcommand(si, CMDLOG_REGISTER, "DROP: \2%s\2", entity(mu)->name);

	(void) hook_call_user_drop(mu);

	if (! nicksvs.no_nick_ownership)
		(void) holdnick_sts(si->service->me, 0, entity(mu)->name, NULL);

	(void) command_success_nodata(si, _("The account \2%s\2 has been dropped."), entity(mu)->name);
	(void) atheme_object_dispose(mu);
}

static void
cmd_ns_fdrop_func(struct sourceinfo *const restrict si, const int ATHEME_VATTR_UNUSED parc, char *parv[])
{
	const char *const acc = parv[0];

	if (! acc)
	{
		(void) command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "FDROP");
		(void) command_fail(si, fault_needmoreparams, _("Syntax: FDROP <account>"));
		return;
	}

	myuser_t *mu;

	if (! (mu = myuser_find(acc)))
	{
		if (!nicksvs.no_nick_ownership)
		{
			mynick_t *const mn = mynick_find(acc);

			if (mn != NULL && command_find(si->service->commands, "FUNGROUP"))
			{
				(void) command_fail(si, fault_nosuch_target, _("\2%s\2 is a grouped nick, use %s "
				                                               "to remove it."), acc, "FUNGROUP");
				return;
			}
		}

		(void) command_fail(si, fault_nosuch_target, _("\2%s\2 is not registered."), acc);
		return;
	}

	if (is_soper(mu))
	{
		(void) command_fail(si, fault_noprivs, _("The nickname \2%s\2 belongs to a services operator; "
		                                         "it cannot be dropped."), acc);
		return;
	}

	if (mu->flags & MU_HOLD)
	{
		(void) command_fail(si, fault_noprivs, _("The account \2%s\2 is held; it cannot be dropped."), acc);
		return;
	}

	(void) wallops("%s dropped the account \2%s\2", get_oper_name(si), entity(mu)->name);
	(void) logcommand(si, CMDLOG_ADMIN | LG_REGISTER, "FDROP: \2%s\2", entity(mu)->name);

	(void) hook_call_user_drop(mu);

	if (! nicksvs.no_nick_ownership)
	{
		mowgli_node_t *n;

		MOWGLI_ITER_FOREACH(n, mu->nicks.head)
		{
			mynick_t *const mn = n->data;

			(void) holdnick_sts(si->service->me, 0, mn->nick, NULL);
		}
	}

	(void) command_success_nodata(si, _("The account \2%s\2 has been dropped."), entity(mu)->name);
	(void) atheme_object_dispose(mu);
}

static struct command cmd_ns_drop = {

	.name           = "DROP",
	.desc           = N_("Drops an account registration."),
	.access         = AC_NONE,
	.maxparc        = 3,
	.cmd            = &cmd_ns_drop_func,

	.help           = {

		.path   = "nickserv/drop",
		.func   = NULL,
	},
};

static struct command cmd_ns_fdrop = {

	.name           = "FDROP",
	.desc           = N_("Forces dropping an account registration."),
	.access         = PRIV_USER_ADMIN,
	.maxparc        = 1,
	.cmd            = &cmd_ns_fdrop_func,

	.help           = {

		.path   = "nickserv/fdrop",
		.func   = NULL,
	},
};

static void
mod_init(struct module ATHEME_VATTR_UNUSED *const restrict m)
{
	(void) service_named_bind_command("nickserv", &cmd_ns_drop);
	(void) service_named_bind_command("nickserv", &cmd_ns_fdrop);
}

static void
mod_deinit(const enum module_unload_intent ATHEME_VATTR_UNUSED intent)
{
	(void) service_named_unbind_command("nickserv", &cmd_ns_drop);
	(void) service_named_unbind_command("nickserv", &cmd_ns_fdrop);
}

SIMPLE_DECLARE_MODULE_V1("nickserv/drop", MODULE_UNLOAD_CAPABILITY_OK)
