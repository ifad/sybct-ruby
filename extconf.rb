require 'mkmf'

## ASE11 linux
# sybase = "/opt/sybase"
# sybase_ocs = "#{sybase}"

## ASE12.5 linux
sybase = "/opt/sybase-12.5"
sybase_ocs = "#{sybase}/OCS-12_5"

## gcc
$LDFLAGS = " -L#{sybase_ocs}/lib " 
# To use the 64-bit OpenClient:                     
# $CFLAGS = "-g -Wall -I#{sybase_ocs}/include -DSYB_LP64"
# To use the 32-bit OpenClient:
$CFLAGS = "-g -Wall -I#{sybase_ocs}/include"

## Note for ALL platforms
# To use the 64-bit OpenClient, you have to link against the 64-bit
# version of the libraries which are named either XXX64 or XXX_r64 (reentrant
# version.)  See the Linux section below.
#
# When Ruby version >= 1.9.1 , use reentrant sybase library if you have.
# (ex. $LOCAL_LIBS = "-lct_r -lcs_r -lsybtcl_r -lcomn_r -lintl_r ....")

## For ASE11 Linux (glibc)
# $LOCAL_LIBS = "-lct -lcs -lsybtcl -lcomn -lintl -linsck -ldl -lm"
#  ( Note:  In some system, there is a conflict between 
#              /usr/lib/libintl.so and $SYBASE/XXX/lib/libintl.so 
#           This case:
#              cd $SYBASE/lib
#              ln -s libintl.so libsybintl.so
#              ln -s libintl.a libsybintl.a
#              /sbin/ldconfig -v
#              ruby extconf.rb ....
# $LOCAL_LIBS = "-lct -lcs -lsybtcl -lcomn -lsybintl -linsck  -ldl -lm"

#### ASE12.5 Linux ###########################
##
## For the 64-bit OpenClient: (Patched by Kiriakos Georgiou and Hiroyuki Sato)
# $LOCAL_LIBS = "-lct64 -lcs64 -lsybtcl64 -lcomn64 -lintl64 -rdynamic -ldl -lnsl -lm"
## For the 64-bit OpenClient and ruby 1.9.1 (thread safe library)
# $LOCAL_LIBS = "-lct64_r -lcs64_r -lsybtcl64_r -lcomn64_r -lintl64_r -rdynamic -ldl -lnsl -lm"
##
## For the 32-bit OpenClient:
$LOCAL_LIBS = "-lct -lcs -lsybtcl -lcomn -lintl -rdynamic -ldl -lnsl -lm"
## For the 32-bit OpenClient and ruby 1.9.1 (thread safe library)
# $LOCAL_LIBS = "-lct_r -lcs_r -lsybtcl_r -lcomn_r -lintl_r -rdynamic -ldl -lnsl -lm"

### ASE15.0.3 Linux 64bit (By Martin Jezek )####################
##   ( Red Hat Enterprise Linux ES release 4 x86_64
##     Adaptive Server Enterprise/15.0.3  ESD#2     )
# sybase = "/opt/ase1503"
# sybase_ocs = "#{sybase}/OCS-15_0"
# $LDFLAGS = " -L#{sybase_ocs}/lib "
# $CFLAGS = "-g -Wall -I#{sybase_ocs}/include -DSYB_LP64"
# $LOCAL_LIBS = "-lsybct64 -lsybcs64 -lsybtcl64 -lsybcomn64 -lsybintl64 -rdynamic -ldl -lnsl -lm"
## When Ruby version >= 1.9.1 , use thread safe library (ex. -lsybct64_r -lsybcs64_r ...)

#########  For Mac OS X 10.4 (By Scott McKenzie)  ##############
# sybase = "/Applications/Sybase/System"
# sybase_ocs = "#{sybase}/OCS-12_5"
# $CFLAGS = "-g -Wall -I#{sybase_ocs}/include"
# $LDFLAGS = " -L#{sybase_ocs}/lib -L#{sybase_ocs}/frameworks/SybaseOpenClient.framework/Versions/Current/PrivateHeaders" 
# $LOCAL_LIBS = "-lsybct -lsybtcl -lsybcs -lsybcomn -lsybintl -lSystem"
# #$LOCAL_LIBS = "-lsybct_r   -lsybtcl_r -lsybcs_r -lsybcomn_r -lsybintl_r  -lSystem"
# $EXTRA_LIBS = "-liconv"   
###########################################################

### Linux + Free TDS (freetds-0.64 or above) ################
# sybase = "/usr/local/freetds"
# $CFLAGS = "-g -Wall -DFREETDS -I#{sybase}/include"
# $LDFLAGS = " -L#{sybase}/lib " 
# $LOCAL_LIBS = "-lct  -lsybdb -ltds -rdynamic -ldl -lnsl -lm"

### mac-osx-tiger + Free TDS (freetds-0.64 ) ################
# sybase = "/usr/local/freetds"
# $CFLAGS = "-g -Wall -DFREETDS -I#{sybase}/include"
# $LDFLAGS = " -L#{sybase}/lib " 
# $LOCAL_LIBS = "-lct  -lsybdb -ltds -lSystem -liconv"

## For SunOS5.X GCC (use dynamic libs) ??
# $LOCAL_LIBS = "-lct -lcs -ltcl -lcomn -lintl -ltli -lnsl"
## For SunOS5.X (use static libs) ??
# $LOCAL_LIBS = "-Xlinker -Bstatic -lct -lcs -ltcl -lcomn -lintl -ltli -Xlinker -Bdynamic -lnsl"

## For HPUX (static)??
# $LOCAL_LIBS = "-a archive  -lct -lcs -ltcl -lcomn -lintl  -linsck"
## For HP (dynamic)??
# $LOCAL_LIBS = "-lct -lcs -ltcl -lcomn -lintl  -linsck"


## DO NOT EDIT BELOW THIS LINE ####################
def ruby_version_high?( ver )
  vruby = RUBY_VERSION.split('.')
  ver = ver.split('.')
  vruby.each{
    |v|
    v = v.to_i
    nver = ver.shift
    return true if( nver.nil?)
    nver = nver.to_i
    return true if( v > nver )
    return false if( v < nver )
  }
  return true if( ver.empty?)
  return false
end

# Check ruby version , check STR2CSTR obsolete?
use_str2cstr = "-DUSE_STR2CSTR"
use_str2cstr = "" if (ruby_version_high?("1.7.0") )

# ruby1.9.1 has not rb_thread_critical, has native thread 
has_thread_critical = "-DHAS_THREAD_CRITICAL"
has_thread_critical = "" if ruby_version_high?("1.9.1")

$CFLAGS = "#{$CFLAGS} #{use_str2cstr} #{has_thread_critical}"
create_makefile("sybct")





