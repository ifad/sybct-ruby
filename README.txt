	       Sybase module for ruby (version 0.2.12 )

===================
General description
===================
  This module is the Sybase extensions to Ruby.

  This module uses Sybase's Client Library. However, every facility of
Client Library is not implemented.
  For example, the followings are not implemented.
     *  Extend Error Data
     *  Dynamic SQL

=======
Copying
=======
  This extension module is copyrighted free software by Tetsuhumi
Takaishi

  You can redistribute it and/or modify it under the same terms as
Ruby itself.

===================
Operating condition
===================
  This module requires ruby-1.3.X (or later) and Sybase's OpenClient
library.

============
Installation
============

1. Please install ruby-1.3.X (or later) and SyBase's Client-Library
beforehand. (I recommend you to use ruby-1.4.2 or later.)

2. type 'gzip -d <sybct-ruby-0.2.9.tar.gz | tar xvf -'

3. type 'cd sybct-ruby-0.2.9'

4. Edit extconf.rb if you need.

  extconf.rb is set for Linux + GCC + ruby1.8 + ASE12.5-32-bit-OpenClinet.

  When you are using it in other environments, In accordance with
environment to use, please change the following descriptions

  * sybase = "/opt/sybase-12.5"
  * sybase_ocs = "#{sybase}/OCS-12_5"
  * $CFLAGS $LDFLAGS  $LOCAL_LIBS 

  ---- Note: Ruby 1.9.1 or above ----
    Ruby 1.9.1 has native thread, so it is better to use sybase thread
   safe `***_r.so` library.
    In my case, (Intel-32bit-Linux + ASE12.5 + ruby1.9.1)
      $LOCAL_LIBS = 
	"-lct_r -lcs_r -lsybtcl_r -lcomn_r -lintl_r -rdynamic -ldl -lnsl -lm"    

5. Generate Makefile
  % ruby extconf.rb

6. Run make 
  % make

7. Testing
  If SyBase SQL server has been working already, It is possible to
confirm the operation by the following commands

    % echo $SYBASE ; echo $SYBASE_OCS  # check the $SYBASE environment variable
    % LD_LIBRARY_PATH="${SYBASE}/${SYBASE_OCS}/lib" ; export LD_LIBRARY_PATH
    % cd sample
    % ruby -I ../ ./sqlsample.rb -S dbserver -U sa -P XXXXXX 
    % ruby -I ../ ./rpcsample.rb  -S dbserver -U sa -P XXXXXX 

	dbserver <-- Name of SyBase server
	XXXXX  <-- Password for sa

8. Install
  Please copy sybct.o sybct.so sybct.rb sybsql.rb to somewhere of
$LOAD_PATH.

   e.g.
     % su
     # cp sybct.o sybct.so sybct.rb sybsql.rb \
            /usr/local/lib/ruby/site_ruby/1.8/i686-linux/

=======
Usage
=======
  Here, I do brief description to use.
  (Please see doc/index.html, about details of methods defined in each
class.)

  Typical programming steps are described below. 
  
(1) Loading the library
  require "sybsql.rb"

(2) Redefinition of Message Callbacks
  In the SybSQLContext class, two methods for message-callbacks are
defined.
	srvmsgCB	----	Server Message Callback
	cltmsgCB	----    Client Message Callback

  If you need, please do redefinition of this two methods.

  Without redefinition, default callback methods will be just applied.

  For closer look, please refer to sybsql.rb source code and
doc/SybSQLContext.html.
  

(3) Establish connection to a server

  Please create SybSQL instance anew. 
         q = SybSQL.new(hash) 
  At this moment,  Connection to the server is established.

  In 'hash', please specify a server name, account information, language
information and etc.
       Typical keys:
             'S' => 'SQL-server-name'
             'U' => 'login-user-name'
             'P' => 'password'
             'lang' => 'locale name'
             'timeout' => Connection timeout (sec)
             'async' => In case of multiple threads application, you must 
                       set this to true.
      (About detailed information, please look at doc/SybSQL.html.)

  The connection is available until a close() method is called or
garbage collector of Ruby finds this.<p>

(4) Send SQL language commands to the server

  * For SQL commands which returns row results (for example,
   select statement),

     Please use the sql() method of a SybSQL class.


  * For  SQL commands which returns no row results (for example,
   insert, use db, etc. ),

     as an easy way, please use the sql_norow() method of a SybSQL
    class.

(5) Check SQL command status
  For the sql() method , Check the return value(true|false) of the
cmd_done?() method. if cmd_done?() returns true, everything went okay.

  For the sql_norow() method , Check the return value(true/false) of
itself.

(6) Obtain Results
  Result Sets which are searched by sql() or sql_norow() method, are
stored in the instance variable of SybSQL as array of SybResult
objects.

  You can retrieve this array with the results() method of SybSQL
class. But usually, you do it with the top_row_result() method of
SybSQL class. The top_row_result() is a method to return the first
regular rows result in ResultSets.


  SybResult class provides the following methods in order to utilize
Result Sets.

  rows() :
        Returns Row Results as array of array.
        [ [ array of 1'st row data], [ array of 2'st row data] ... \
          [ array of nth row data] ]

  nthrow(nth, clm=nil ) :
	When 'clm' was omitted,
                Returns nth Row Results array
	If 'clm' is number,
                Returns a data at the specified row number and column
	       number.
                'clm' means column number.
        If 'clm' is a kind of String object,
                Returns a data at the specified row number and column
	       name.
                'clm' means column name.

  columns() :
	Returns column names as String-Array.


(7) Close a server connection
  Connection is cut off by the close() method of SybSQL class.


* A Simple Example *
  Here is an example of using this library

      require "sybsql.rb"  # Load the extension library
      begin

	# Connection to the server, 
        #     server name --  'SYBASE'
        #     user name ----  'sa',
        #     password  ----  'PassWord'
	query=SybSQL.new( {'S'=>'SYBASE', 'U'=>'sa', 'P'=>'PassWord'} )

	# use the mydb database
	raise "ERROR: use mydb ..." unless( query.sql_norow("use mydb") )

	# Sending a sql-command to the server
	query.sql("select * from theTable")

	# Check errors
	raise "ERROR: select ..." unless (query.cmd_done? )

	# Gets the first RowResults to 'res'
	res = query.top_row_result

	# Print column names.
	print res.columns.join(','), "\n"

	# Print row datas
	res.rows.each {
	  |r| print r.join('|'), "\n"
	}

      rescue
	# Error!
	print $!.to_s,"\n"

      ensure
	# Finishing up
	query.close if(query.kind_of?( SybSQL ) )
      end

=================
Example programs.
=================
  The examples and their source files are listed below.

sample/getimage.rb
------------------
  Shows how to retrieve data from a image datatype column. 
  
sample/sendimage.rb
-------------------
  Shows how to insert and update data from a image datatype column. 

cursor_disp.rb, cursor_update.rb
------------------------------------
  The examples for Client-Library cursor.

sample/isql.rb
--------------
  This is a sample such as the isql command.
  (requires the readline module)

=============================================
The environment that I work, and is confirmed
=============================================
  As for this module, it is confirmed action only with the following
environment.

   * OS:	Linux (Vine Linux 3.0)
   * Ruby:	ruby 1.4.2 , ruby 1.6.3,  ruby 1.8.2
   * Sybase:	Adaptive Server Enterprise 11.0.3E6 and 12.5

=====================
Client-Library Cursor
=====================
  In this version , the following functions does NOT supported.
    * Redefinition the current row .
    * Re-open the cursor
    * Using host variables 


======================================
sybct-ruby-0.2.11 ==> sybct-ruby-0.2.12
======================================
* Fixed compile error on Ruby 1.9.1.
  (Thanks to Bob Saveland )

======================================
sybct-ruby-0.2.10 ==> sybct-ruby-0.2.11
======================================
* applied Ruby-DBI patches from Charles.  thanks.
  - allow_serveraddr_prop.patch (this is to allow connection by
    specification of CS_SERVERADDR);
  - expose_column_types.patch (to allow introspection on column types
    of result sets); and
  - extra_defines_for_freetds.patch (these were neccessary to compile
    with freetds).

* => sybct-ruby-0.2.11_p1
  *extconf.rb 
    ASE15.0.3 Linux 64bit compile options. 
    (Thanks to Martin Jezek )

======================================
sybct-ruby-0.2.9 ==> sybct-ruby-0.2.10
======================================
* Modified extconf.rb
  * 64bit OpenClient compile options. 
    (Thanks to Hiroyuki Sato and Kiriakos Georgiou)
  * Add freetds under Mac-OSX-tiger

=====================================
sybct-ruby-0.2.8 ==> sybct-ruby-0.2.9
=====================================
* Support freetds (freetds-0.64 or above ).
  It is not yet stable, so some functionality may not work as expected.
  The following functions does not work.
   * SybSQL.new 
     'lang=>' and 'async=>true' option not work
   * SybCommand        
      cursor_option, cursor_update and cursor_delete functions not work.
  
  Please edit /usr/local/FREETDS/freetds.conf, locales.conf, in order to define
the Db-server and locales. (See freetds's documents)
  ex. `freetds.conf` japanese setting
     [your-DBserver-name]
	host = your-host
	port = 7500
	tds version = 5.0
        charset = eucjis
  ex. `locales.conf` japanese setting
     [ja_JP.eucJP]
        language = japanese
        charset = eucjis
	date format = %Y/%m/%d %H:%M:%S

* README-cygwin.txt

=====================================
sybct-ruby-0.2.7 ==> sybct-ruby-0.2.8
=====================================
* extconf.rb
  Description in case of Mac OS x 10.4 (By Scott McKenzie)

* Change SYB_CTLIB_VERSION to the latest CS_VERSION_nnn. 
  ( Thanks to Will Sobel )

=====================================
sybct-ruby-0.2.4 ==> sybct-ruby-0.2.7
=====================================
* Fixed some bugs.

* extconf.rb
  Description in case of ASE12.5

* SybSQL#new 'login-timeout' 
* SybSQL#connection_status 
* SybSQL#connection_dead?  


==========
The Author
==========
  Tetsuhumi Takaishi (Email: tetsuhumi@aa.bb-east.ne.jp)

===========
NO WARRANTY
===========
  THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF
MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.


