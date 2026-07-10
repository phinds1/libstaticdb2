# DB2 delta system

CLI app to execute SQL DDL and SQL changes into the database, wwritten in ./db2delta with its own sub Makefile called by the main Makefile

Liquibase is a pain

you need either a Db2 cli install which required the clien lib in the apps, /home.db2inst1 and 50 mb user install and then you have to catalog nodes
or you use liquibase over JDBC and you need Java and the jar and a folder of fugly XML files.

db2delta can be much better,

db2delta -d hostname:port/database -u db2inst1 (-p password) -s sql_files...
db2delta -d hostname:port/database -q "SELECT ..."   # arbitrary sqls to validate the deltas worked, or hack at a broken delta to fix it,
db2delta -d hostname:port/database -V # show database version
`-x` for scripting with the tool. e.g. writing scripts to confirm that the deltas worked
`-c|continue-on-error` support deefaults to fail fast

- Requires folder of text files
- Files are in Ascibettical order, typicall iso date and a name `.delta`
- Format is like .ini format (but not)
  - file has a header with
	- `terminator=` (default to `;`, `@` for procedures)
	- `continue-on-error=` true|false  (useful for changesets that are desinged to fix broken installations, that might be more or less broken in each environment
	- `logging=` debug|warn|error
	- plus any old meta data like `author=`
  - each changeset has a name in [changeset:name]
  - contents is pretty much just SQL, designed to be directly copy pastable.
  - change is recorded in the DB, in a DBDELTA_CHANGELOG table (like liquibase does) with the change set name a timestamp and a hash.
  - file also suporte [validation:name]  this is a list of typically read only SQL that validate the that delta ran OK
- no rollback facilities, presumes CI/CD where each change breaks nothing.
  
When run, by default, it checks all the files and runs what is missing from the database.

db2delta itself is a musl compiled binary, with no dependencies.
if there is a `./database.env` file it contains CLI args so, in the most simple case, just running `db2delta` works.

Simples

