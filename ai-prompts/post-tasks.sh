

run /security


Testing should be sdone using snip.  This way we can test actual code in the .c files
Wwe should copy /java/czh/dgsubs/bin scripts and the  mechanism for creating docker db2 instances.
the db2 isntance should be different create a dedicated test database.
we need perf testing
we need fuzzzing
we need instegration testing 
we need a better than db2 cli, one that does not return silly error codes for warning so it integrations with scripts using `set -e` better
  it should support `-x` and other featurs fromthe db2 cli but should not copy any code from IBM
  accept a jdbc like url on the command line db2://hostname:port/PDDB username and password with option for entering on the command line

Do a better than liquibase tool
  folders of directories
  like .ini fomrmat (but not)
  each change set gets a name
  contents is just SQL
  change is hashed and stick in the DB like liquibase does  
