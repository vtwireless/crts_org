/*-
 * See the file LICENSE for redistribution information.
 *
 * Copyright (c) 1997-2003
 *	Sleepycat Software.  All rights reserved.
 *
 * $Id: db4Tpcb.cpp,v 1.1.1.1 2007-01-10 21:14:16 lthompso Exp $
 */

/*-
 *
 *	Threaded db4 exampled intended for the logistics application.
 *	Mostly sleepycat example code, but see:
 *  void * db4_tpcb_thrd (void * context) 
 *
 */

#if defined (PROC_THREADS)	// covers whole file

#include <sys/types.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "pthread.h"

/* #include <iostream>  */
/* #include <iomanip>  */
#include <db_cxx.h>
#include "processor.h"


//	jm: stub out this one - some conflict with stlport ??
//
void DbEnv::set_error_stream(_STL::ostream *x)
{
	Log (0, "Tpcb: ERROR:  Should not reach this\n");
}

typedef enum { ACCOUNT, BRANCH, TELLER } FTYPE;

static int	  invarg(int, char *);
u_int32_t random_id(FTYPE, u_int32_t, u_int32_t, u_int32_t);
u_int32_t random_int(u_int32_t, u_int32_t);
static int	  usage(void);

int verbose;
const char *progname = "TpcbExample";                       // Program name.

class TpcbExample : public DbEnv
{
public:
	void populate(int, int, int, int);
	void run(int, int, int, int);
	int txn(Db *, Db *, Db *, Db *,
		int, int, int);
	void populateHistory(Db *, int, u_int32_t, u_int32_t, u_int32_t);
	void populateTable(Db *, u_int32_t, u_int32_t, int, const char *);

	// Note: the constructor creates a DbEnv(), which is
	// not fully initialized until the DbEnv::open() method
	// is called.
	//
	TpcbExample(const char *home, int cachesize,
		    int initializing, int flags);

private:
	static const char FileName[];

	// no need for copy and assignment
	TpcbExample(const TpcbExample &);
	void operator = (const TpcbExample &);
};

//
// This program implements a basic TPC/B driver program.  To create the
// TPC/B database, run with the -i (init) flag.  The number of records
// with which to populate the account, history, branch, and teller tables
// is specified by the a, s, b, and t flags respectively.  To run a TPC/B
// test, use the n flag to indicate a number of transactions to run (note
// that you can run many of these processes in parallel to simulate a
// multiuser test run).
//
#define	TELLERS_PER_BRANCH      100
#define	ACCOUNTS_PER_TELLER     1000
#define	HISTORY_PER_BRANCH	2592000

/*
 * The default configuration that adheres to TPCB scaling rules requires
 * nearly 3 GB of space.  To avoid requiring that much space for testing,
 * we set the parameters much lower.  If you want to run a valid 10 TPS
 * configuration, define VALID_SCALING.
 */
#ifdef	VALID_SCALING
#define	ACCOUNTS	 1000000
#define	BRANCHES	      10
#define	TELLERS		     100
#define	HISTORY		25920000
#endif

#ifdef	TINY
#define	ACCOUNTS	    1000
#define	BRANCHES	      10
#define	TELLERS		     100
#define	HISTORY		   10000
#endif

#if !defined(VALID_SCALING) && !defined(TINY)
#define	ACCOUNTS	  100000
#define	BRANCHES	      10
#define	TELLERS		     100
#define	HISTORY		  259200
#endif

#define	HISTORY_LEN	    100
#define	RECLEN		    100
#define	BEGID		1000000

struct Defrec {
	u_int32_t   id;
	u_int32_t   balance;
	u_int8_t    pad[RECLEN - sizeof(u_int32_t) - sizeof(u_int32_t)];
};

struct Histrec {
	u_int32_t   aid;
	u_int32_t   bid;
	u_int32_t   tid;
	u_int32_t   amount;
	u_int8_t    pad[RECLEN - 4 * sizeof(u_int32_t)];
};

int run_tpcb (int argc, char *argv[]) 
{
	unsigned long seed;
	int accounts, branches, tellers, history;
	int iflag, mpool, ntxns, txn_no_sync;
	const char *home;
	char *endarg;

	home = "../Test";
	accounts = branches = history = tellers = 0;
	txn_no_sync = 0;
	mpool = ntxns = 0;
	verbose = 0;
	iflag = 0;
	seed = (unsigned long)time(NULL);

	for (int i = 1; i < argc; ++i) {

		if (strcmp(argv[i], "-a") == 0) {
			// Number of account records
			if ((accounts = atoi(argv[++i])) <= 0)
				return (invarg('a', argv[i]));
		}
		else if (strcmp(argv[i], "-b") == 0) {
			// Number of branch records
			if ((branches = atoi(argv[++i])) <= 0)
				return (invarg('b', argv[i]));
		}
		else if (strcmp(argv[i], "-c") == 0) {
			// Cachesize in bytes
			if ((mpool = atoi(argv[++i])) <= 0)
				return (invarg('c', argv[i]));
		}
		else if (strcmp(argv[i], "-f") == 0) {
			// Fast mode: no txn sync.
			txn_no_sync = 1;
		}
		else if (strcmp(argv[i], "-h") == 0) {
			// DB  home.
			home = argv[++i];
		}
		else if (strcmp(argv[i], "-i") == 0) {
			// Initialize the test.
			iflag = 1;
		}
		else if (strcmp(argv[i], "-n") == 0) {
			// Number of transactions
			if ((ntxns = atoi(argv[++i])) <= 0)
				return (invarg('n', argv[i]));
		}
		else if (strcmp(argv[i], "-S") == 0) {
			// Random number seed.
			seed = strtoul(argv[++i], &endarg, 0);
			if (*endarg != '\0')
				return (invarg('S', argv[i]));
		}
		else if (strcmp(argv[i], "-s") == 0) {
			// Number of history records
			if ((history = atoi(argv[++i])) <= 0)
				return (invarg('s', argv[i]));
		}
		else if (strcmp(argv[i], "-t") == 0) {
			// Number of teller records
			if ((tellers = atoi(argv[++i])) <= 0)
				return (invarg('t', argv[i]));
		}
		else if (strcmp(argv[i], "-v") == 0) {
			// Verbose option.
			verbose = 1;
		}
		else {
			return (usage());
		}
	}

	srand((unsigned int)seed);

	accounts = accounts == 0 ? ACCOUNTS : accounts;
	branches = branches == 0 ? BRANCHES : branches;
	tellers = tellers == 0 ? TELLERS : tellers;
	history = history == 0 ? HISTORY : history;

	/*******
	  if (verbose)
		cout << (long)accounts << " Accounts, "
		     << (long)branches << " Branches, "
		     << (long)tellers << " Tellers, "
		     << (long)history << " History\n";
	*******/

	try {
		// Initialize the database environment.
		// Must be done in within a try block, unless you
		// change the error model in the environment options.
		//
		TpcbExample app(home, mpool, iflag,
				txn_no_sync ? DB_TXN_NOSYNC : 0);

		if (iflag) {
			if (ntxns != 0)
				return (usage());
			app.populate(accounts, branches, history, tellers);
		}
		else {
			if (ntxns == 0)
				return (usage());
			app.run(ntxns, accounts, branches, tellers);
		}

		app.close(0);
		return (EXIT_SUCCESS);
	}
	catch (DbException &dbe) {
		Log (0, "TpcbExample Error: %s\n", dbe.what() );
		return (EXIT_FAILURE);
	}
}

static int
invarg(int arg, char *str)
{
	Log (0, "TpcbExample Error: invalid argument for \"%c\": <%s>\n", 
			(char) arg,  str);
	return (EXIT_FAILURE);
}

static int
usage()
{
	Log (0, "TpcbExample Usage Error\n" );
	return (EXIT_FAILURE);
}

static void catch_error_strings (const char *arg1, char *arg2)
{
	Log (0, "TpcbExample: Error, caught error strings: %s, %s\n",
			(arg1 && arg1 != NULL) ? arg1 : "NULL",
			(arg2 && arg2 != NULL) ? arg2 : "NULL" );
}

TpcbExample::TpcbExample(const char *home, int cachesize,
			 int initializing, int flags)
:	DbEnv(0)
{
	u_int32_t local_flags;

	set_errcall (catch_error_strings);  
	/* set_errpfx("TpcbExample"); */

	(void)set_cachesize(0, cachesize == 0 ?
			    4 * 1024 * 1024 : (u_int32_t)cachesize, 0);

	if (flags & (DB_TXN_NOSYNC))
		set_flags(DB_TXN_NOSYNC, 1);
	flags &= ~(DB_TXN_NOSYNC);

	local_flags = flags | DB_CREATE | DB_INIT_MPOOL;
	if (!initializing)
		local_flags |= DB_INIT_TXN | DB_INIT_LOCK | DB_INIT_LOG;
	open(home, local_flags, 0);
}

//
// Initialize the database to the specified number of accounts, branches,
// history records, and tellers.
//
void
TpcbExample::populate(int accounts, int branches, int history, int tellers)
{
	Db *dbp;

	int err;
	u_int32_t balance, idnum;
	u_int32_t end_anum, end_bnum, end_tnum;
	u_int32_t start_anum, start_bnum, start_tnum;

	idnum = BEGID;
	balance = 500000;

	dbp = new Db(this, 0);
	dbp->set_h_nelem((unsigned int)accounts);

	if ((err = dbp->open(NULL, "account", NULL, DB_HASH,
			     DB_CREATE | DB_TRUNCATE, 0644)) != 0) {
		DbException except("Account file create failed", err);
		throw except;
	}

	start_anum = idnum;
	populateTable(dbp, idnum, balance, accounts, "account");
	idnum += accounts;
	end_anum = idnum - 1;
	if ((err = dbp->close(0)) != 0) {
		DbException except("Account file close failed", err);
		throw except;
	}
	delete dbp;

	if (verbose)
		Log (0, "db4example: Populated accounts: %ld - %ld\n",
		     (long)start_anum,  (long)end_anum );

	dbp = new Db(this, 0);
	//
	// Since the number of branches is very small, we want to use very
	// small pages and only 1 key per page.  This is the poor-man's way
	// of getting key locking instead of page locking.
	//
	dbp->set_h_ffactor(1);
	dbp->set_h_nelem((unsigned int)branches);
	dbp->set_pagesize(512);

	if ((err = dbp->open(NULL, "branch", NULL, DB_HASH,
			     DB_CREATE | DB_TRUNCATE, 0644)) != 0) {
		DbException except("Branch file create failed", err);
		throw except;
	}
	start_bnum = idnum;
	populateTable(dbp, idnum, balance, branches, "branch");
	idnum += branches;
	end_bnum = idnum - 1;
	if ((err = dbp->close(0)) != 0) {
		DbException except("Close of branch file failed", err);
		throw except;
	}
	delete dbp;


	dbp = new Db(this, 0);
	//
	// In the case of tellers, we also want small pages, but we'll let
	// the fill factor dynamically adjust itself.
	//
	dbp->set_h_ffactor(0);
	dbp->set_h_nelem((unsigned int)tellers);
	dbp->set_pagesize(512);

	if ((err = dbp->open(NULL, "teller", NULL, DB_HASH,
			     DB_CREATE | DB_TRUNCATE, 0644)) != 0) {
		DbException except("Teller file create failed", err);
		throw except;
	}

	start_tnum = idnum;
	populateTable(dbp, idnum, balance, tellers, "teller");
	idnum += tellers;
	end_tnum = idnum - 1;
	if ((err = dbp->close(0)) != 0) {
		DbException except("Close of teller file failed", err);
		throw except;
	}
	delete dbp;

	dbp = new Db(this, 0);
	dbp->set_re_len(HISTORY_LEN);
	if ((err = dbp->open(NULL, "history", NULL, DB_RECNO,
			     DB_CREATE | DB_TRUNCATE, 0644)) != 0) {
		DbException except("Create of history file failed", err);
		throw except;
	}

	populateHistory(dbp, history, accounts, branches, tellers);
	if ((err = dbp->close(0)) != 0) {
		DbException except("Close of history file failed", err);
		throw except;
	}
	delete dbp;
}

void
TpcbExample::populateTable(Db *dbp,
			   u_int32_t start_id, u_int32_t balance,
			   int nrecs, const char *msg)
{
	Defrec drec;
	memset(&drec.pad[0], 1, sizeof(drec.pad));

	Dbt kdbt(&drec.id, sizeof(u_int32_t));
	Dbt ddbt(&drec, sizeof(drec));

	for (int i = 0; i < nrecs; i++) {
		drec.id = start_id + (u_int32_t)i;
		drec.balance = balance;
		int err;
		if ((err =
		     dbp->put(NULL, &kdbt, &ddbt, DB_NOOVERWRITE)) != 0) 
		{
			Log (0, "TpcbExample Error Initializing: %s file: %s\n", 
					msg,  strerror(err) );
			DbException except("failure initializing file", err);
			throw except;
		}
	}
}

void
TpcbExample::populateHistory(Db *dbp, int nrecs, u_int32_t accounts,
			     u_int32_t branches, u_int32_t tellers)
{
	Histrec hrec;
	memset(&hrec.pad[0], 1, sizeof(hrec.pad));
	hrec.amount = 10;
	db_recno_t key;

	Dbt kdbt(&key, sizeof(u_int32_t));
	Dbt ddbt(&hrec, sizeof(hrec));

	for (int i = 1; i <= nrecs; i++) {
		hrec.aid = random_id(ACCOUNT, accounts, branches, tellers);
		hrec.bid = random_id(BRANCH, accounts, branches, tellers);
		hrec.tid = random_id(TELLER, accounts, branches, tellers);

		int err;
		key = (db_recno_t)i;
		if ((err = dbp->put(NULL, &kdbt, &ddbt, DB_APPEND)) != 0) {
			DbException except("failure initializing history file",
					   err);
			throw except;
		}
	}
}

u_int32_t
random_int(u_int32_t lo, u_int32_t hi)
{
	u_int32_t ret;
	int t;

	t = rand();
	ret = (u_int32_t)(((double)t / ((double)(RAND_MAX) + 1)) *
			  (hi - lo + 1));
	ret += lo;
	return (ret);
}

u_int32_t
random_id(FTYPE type, u_int32_t accounts, u_int32_t branches, u_int32_t tellers)
{
	u_int32_t min, max, num;

	max = min = BEGID;
	num = accounts;
	switch (type) {
	case TELLER:
		min += branches;
		num = tellers;
		// Fallthrough
	case BRANCH:
		if (type == BRANCH)
			num = branches;
		min += accounts;
		// Fallthrough
	case ACCOUNT:
		max = min + num - 1;
	}
	return (random_int(min, max));
}

void
TpcbExample::run(int n, int accounts, int branches, int tellers)
{
	Db *adb, *bdb, *hdb, *tdb;
	double gtps, itps;
	int failed, ifailed, ret, txns;
	time_t starttime, curtime, lasttime;

	//
	// Open the database files.
	//

	int err;
	adb = new Db(this, 0);
	if ((err = adb->open(NULL, "account", NULL, DB_UNKNOWN,
			     DB_AUTO_COMMIT, 0)) != 0) {
		DbException except("Open of account file failed", err);
		Log (0, "TpcbExample Error \n"); 
		throw except;
	}

	bdb = new Db(this, 0);
	if ((err = bdb->open(NULL, "branch", NULL, DB_UNKNOWN,
			     DB_AUTO_COMMIT, 0)) != 0) {
		DbException except("Open of branch file failed", err);
		Log (0, "TpcbExample Error \n"); 
		throw except;
		throw except;
	}

	tdb = new Db(this, 0);
	if ((err = tdb->open(NULL, "teller", NULL, DB_UNKNOWN,
			     DB_AUTO_COMMIT, 0)) != 0) {
		DbException except("Open of teller file failed", err);
		Log (0, "TpcbExample Error \n"); 
		throw except;
		throw except;
	}

	hdb = new Db(this, 0);
	if ((err = hdb->open(NULL, "history", NULL, DB_UNKNOWN,
			     DB_AUTO_COMMIT, 0)) != 0) {
		DbException except("Open of history file failed", err);
		Log (0, "TpcbExample Error \n"); 
		throw except;
		throw except;
	}

	txns = failed = ifailed = 0;
	starttime = time(NULL);
	lasttime = starttime;
	while (n-- > 0) {
		txns++;
		ret = txn(adb, bdb, tdb, hdb, accounts, branches, tellers);
		if (ret != 0) {
			failed++;
			ifailed++;
		}
		if (n % 5000 == 0) {
			curtime = time(NULL);
			gtps = (double)(txns - failed) / (curtime - starttime);
			itps = (double)(5000 - ifailed) / (curtime - lasttime);

			// We use printf because it provides much simpler
			// formatting than iostreams.
			//
			Log (0, "Tpcb: %d txns,  %d failed, %6.2f TPS (gross) %6.2f TPS (interval)\n",
				txns, failed, gtps, itps);

			lasttime = curtime;
			ifailed = 0;
		}
	}

	(void)adb->close(0);
	(void)bdb->close(0);
	(void)tdb->close(0);
	(void)hdb->close(0);

	Log (0, "Tpcb: transactions begun: %ld  [failed?: %ld]\n",
			(long) txns, (long)failed );
}

//
// XXX Figure out the appropriate way to pick out IDs.
//
int
TpcbExample::txn(Db *adb, Db *bdb, Db *tdb, Db *hdb,
		 int accounts, int branches, int tellers)
{
	Dbc *acurs = NULL;
	Dbc *bcurs = NULL;
	Dbc *tcurs = NULL;
	DbTxn *t = NULL;

	db_recno_t key;
	Defrec rec;
	Histrec hrec;
	int account, branch, teller, ret;

	Dbt d_dbt;
	Dbt d_histdbt;
	Dbt k_dbt;
	Dbt k_histdbt(&key, sizeof(key));

	//
	// XXX We could move a lot of this into the driver to make this
	// faster.
	//
	account = random_id(ACCOUNT, accounts, branches, tellers);
	branch = random_id(BRANCH, accounts, branches, tellers);
	teller = random_id(TELLER, accounts, branches, tellers);

	k_dbt.set_size(sizeof(int));

	d_dbt.set_flags(DB_DBT_USERMEM);
	d_dbt.set_data(&rec);
	d_dbt.set_ulen(sizeof(rec));

	hrec.aid = account;
	hrec.bid = branch;
	hrec.tid = teller;
	hrec.amount = 10;
	// Request 0 bytes since we're just positioning.
	d_histdbt.set_flags(DB_DBT_PARTIAL);

	// START TIMING
	if (txn_begin(NULL, &t, 0) != 0)
		goto err;

	if (adb->cursor(t, &acurs, 0) != 0 ||
	    bdb->cursor(t, &bcurs, 0) != 0 ||
	    tdb->cursor(t, &tcurs, 0) != 0)
		goto err;

	// Account record
	k_dbt.set_data(&account);
	if (acurs->get(&k_dbt, &d_dbt, DB_SET) != 0)
		goto err;
	rec.balance += 10;
	if (acurs->put(&k_dbt, &d_dbt, DB_CURRENT) != 0)
		goto err;

	// Branch record
	k_dbt.set_data(&branch);
	if (bcurs->get(&k_dbt, &d_dbt, DB_SET) != 0)
		goto err;
	rec.balance += 10;
	if (bcurs->put(&k_dbt, &d_dbt, DB_CURRENT) != 0)
		goto err;

	// Teller record
	k_dbt.set_data(&teller);
	if (tcurs->get(&k_dbt, &d_dbt, DB_SET) != 0)
		goto err;
	rec.balance += 10;
	if (tcurs->put(&k_dbt, &d_dbt, DB_CURRENT) != 0)
		goto err;

	// History record
	d_histdbt.set_flags(0);
	d_histdbt.set_data(&hrec);
	d_histdbt.set_ulen(sizeof(hrec));
	if (hdb->put(t, &k_histdbt, &d_histdbt, DB_APPEND) != 0)
		goto err;

	if (acurs->close() != 0 || bcurs->close() != 0 || tcurs->close() != 0)
		goto err;

	ret = t->commit(0);
	t = NULL;
	if (ret != 0)
		goto err;

	// END TIMING
	return (0);

err:
	if (acurs != NULL)
		(void)acurs->close();
	if (bcurs != NULL)
		(void)bcurs->close();
	if (tcurs != NULL)
		(void)tcurs->close();
	if (t != NULL)
		(void)t->abort();

	return (-1);
}


/**
 *	a pthread_create() entry point to kick this off.
 *	one-off operation is best, this cranks a bunch! of cycles.
 *
 */

void * db4_tpcb_thrd (void * context) 
{
	int * continual = (int *) context;
	static int x  = 0;
	/* void *pusharg;  */
	/* int	  pushint = 666; */
	char arg2 [100];
	char *args[4]  = {"tpcb", "-n"} ;

	/* sprintf (arg2, "x.%x", (int) get_thrd_id ()); */
	strcpy (arg2, "50");

	args[2] = arg2;
	args[3] = NULL;
	
	/* do push here if I can get it to work... */
	/* pusharg = (void *) &pushint; */
	/* pthread_cleanup_push (dirac_thread_exit_msg, pusharg);	// broken */
	/* pthread_cleanup_pop (0); */

	do 
	{
		Log (0, "Tpcb thread running %s tranactions %s %s\n", 
				arg2, (*continual) ? "- continuallly" : "");

		x = run_tpcb (3, args);

		(void) pthread_testcancel();

	} while (*continual);

	Log (0, "Tpcb: exiting - exit code %d\n", x);

	return (void *) &x;

}


#endif // PROC_THREADS


// endofile
