#! /usr/local/bin/ruby
# The example for using a Client-Library cursor. 
# This example relies on the pubs2 database.
#                            (see $SYBASE/scripts/installpubs2 )
#  
# ex.
#   $ LANG=ja_JP.ujis ; export LANG
#   $ ruby cursor_disp.rb -S SYBASE -U sa -P XXXXXX 
#

require "sybsql.rb"

lang = ENV['LANG']
lang = 'default' unless lang

server = "SYBASE"
user = "sa"
passwd = ""
timeout = 0

while ( !ARGV.empty? )			# H.NAKAMURA patch
    case ARGV.shift
    when /^-S$/, /^-S(.*)$/
      server = $1 || ARGV.shift
    when /^-U$/, /^-U(.*)$/
      user = $1 || ARGV.shift
    when /^-P$/, /^-P(.*)$/
      passwd = $1 || ARGV.shift
    when /^-J$/, /^-J(.*)$/
      lang = $1 || ARGV.shift
    when /^-t$/, /^-t(.*)$/
      timeout = $1 || ARGV.shift
      timeout = timeout.to_i
    else
      $stderr.print "usage: ",$0,
	"  [-S server -U user -P passwd -J lang -t timeout-sec]\n"
      exit 1
    end
end

print "server=",server," user=", user,
    " passwd=",passwd," lang=",lang," timeout=",timeout,"\n"

begin
  query=SybSQL.new(
		 {'S'=>server,'U'=>user,'P'=>passwd, 'timeout' => timeout,
		     'appname'=>'rubysample','lang'=>lang,
		     'async'=>true,
		   })

  # use db
  unless( query.sql_norow("use pubs2") ) then
    raise "failed, use pubs2"
  end
  
  cnt = 0
  query.do_cursor( "a cursor", 
		  "select au_fname, au_lname, postalcode from authors"
		  ){
    |cm,st,clm,row|
    print clm.join(","),"\n" if( cnt <= 0 )
    cnt += 1
    print row.join("|"),"\n"
    nil
  }
rescue 
  print "ERROR : ", $!,"\n"
ensure
  query.close if(query.kind_of?( SybSQL ) );
  query = nil
end
