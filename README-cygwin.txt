 `sybct-ruby` under Cygwin works in the following ways.

 However I have no knowledge of Windows programming.
 So, I do not understand at all why this works.
 Please don't ask me for details. 

=====================
My cygwin environment
=====================
$ uname -a
  CYGWIN_NT-5.1 1.5.19(0.150/4/2) 2006-01-20 13:28 i686 Cygwin

$ ruby -v
  ruby 1.8.4 (2005-12-24) [i386-cygwin]

$ gcc -v
  Thread model: posix
  gcc version 3.4.4 (cygming special) (gdc 0.12, using dmd 0.125)

===============
ASE OCS install
===============
 `sybct-ruby` uses  Sybase's Open-Client Library
  Please skip , if you already have this.

* Download open client library
  URL:   http://www.sybase.com/linuxpromo
  Click the link ``Free Software Download for Windows Clients``

  Run setup.exe, and install at least `Open client/server`.

* Reboot Windows

* Create $SYBASE/ini/sql.ini
  For example:
    when DB-server name -- mysybase
         DB-server's IP address:port -- 192.168.10.9:7100

  $ cat /cygdrive/c/sybase/ini/sql.ini
    [mysybase]
    master=TCP,192.168.10.9,7100
    query=TCP,192.168.10.9,7100
  $
  
* Check by using isql.exe
  LANG=enu ; export LANG
  $SYBASE/$SYBASE_OCS/bin/isql.exe -U sa -S mysybase -P "XXXX"

============================
Install sybct-ruby on cygwin
============================
* Extract tarball
  $ tar zxvf sybct-ruby-0.2.9.tar.gz 
  $ cd sybct-ruby-0.2.9

* Make libcs.a libct.a

  $ mkdir cygwin-lib
  $ cd cygwin-lib/

  $ echo EXPORTS > libcs.def
  $ nm $SYBASE/$SYBASE_OCS/lib/libcs.lib | \
    grep ' T _' | sed 's/.* T _//' >> libcs.def

  $dlltool -d libcs.def --dllname libcs.dll  --output-lib libcs.a -k

  $ echo EXPORTS > libct.def
  $ nm $SYBASE/$SYBASE_OCS/lib/libct.lib | \
    grep ' T _' | sed 's/.* T _//' >> libct.def

  $ dlltool -d libct.def --dllname libct.dll  --output-lib libct.a -k

  $ ls -l
     total 58
    -rw-rw-rw- 1 20216  libcs.a
    -rw-rw-rw- 1   401  libcs.def
    -rw-rw-rw- 1 36576  libct.a
    -rw-rw-rw- 1   709  libct.def

  $ cd ..

* Please edit extconf-cygwin.rb to change `sybase` path 

* Compile and Link

  $ ruby extconf-cygwin.rb
  $ make
  
* Test 
  $ cd sample
  $ LANG=enu ; export LANG 

  $ ruby -I ../ ./sqlsample.rb -S mysybase -U sa -P "XXXX"

  $ ruby -I ../ ./rpcsample.rb -S mysybase -U sa -P "XXXX"

  $ cd ..

* Install
  cp sybct.o sybct.so sybct.rb sybsql.rb \
     /usr/local/lib/ruby/site_ruby/1.8/i386-cygwin/

=====
 BUG
=====
 On some Windows system, the following error occurs.

     $ echo $SYBASE                            # Check $SYBASE
       => C:\sybase
     $ ruby -I ./ sample/sqlsample.rb -S ...   # Exec sybct sample 
       =>
	 "Your sybase home directory is c:\sql10. Check the environment
	 variable SYBASE if it is not the one you want!"

  I think $SYBASE system environment variable is not recognized in
`sybct-ruby` for some reason. 
  I have no idea to fix this error.

  But it works by either of the following three ways.
  
  * Using Control Panel, Set SYBASE variable to both System and User
                                                                ~~~~~
    environment.

OR

  * Copy $SYBASE/ to c:\sql10
     cd /cygdrive/c
     mkdir sql10
     cp -a sybase/ini sql10/
     cp -a sybase/locales sql10/ 
     cp -a sybase/charsets sql10/
OR

  * Not use bash, use cmd.exe
    
