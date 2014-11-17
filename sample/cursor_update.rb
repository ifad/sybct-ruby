#! /usr/local/bin/ruby
# The example for using a Client-Library cursor. 
# This example relies on the pubs2 database.
#                            (see $SYBASE/scripts/installpubs2 )
#  
# ex.
#   $ LANG=ja_JP.ujis ; export LANG
#   $ ruby cursor_update.rb -S SYBASE -U sa -P XXXXXX 
#
# NOTE: this is not work with FreeTDS

require "sybsql.rb"

lang = ENV['LANG']
lang = 'default' unless lang

server = "SYBASE"
user = "sa"
passwd = ""
timeout = 0

while ( !ARGV.empty? )	
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

  unless( query.sql_norow("begin tran") ) then
    raise "failed"
  end
    
  $stderr.print "cursor_disp begin\n" if( $DEBUG )
  print "\n=============  Original data =================\n"
  query.do_cursor( "a cursor", 
		  "select title, price , total_sales from titles " +
		  "for update of price"
		  ){
    |cm,st,clm,row|
    print row.join("|"),"\n"
    price = row[1]
    if( ! price.nil? ) then
      newprice = price.to_f * 2.0  # newprice = 2 * price
      # print "NewPrice=#{newprice}","\n"

      # Execute a nested UPDATE
      if( cm.cursor_update("titles",
			   "update titles set price = #{newprice}"))then
	resup = query.query(cm)
	raise "failed, update" unless (resup.kind_of?(SybResultArray))
	raise "failed, update" if( resup.cmd_fail?)
      end

    end
    nil
  }
  
  print "\n=============  After update =================\n"
  cnt = 0
  query.sql("select title, price , total_sales from titles"){
    |cm,st,clm,row|
    print clm.join(","),"\n" if( cnt <= 0 )
    cnt += 1
    print row.join("|"),"\n"
    nil
  }
rescue 
  print "ERROR : ", $!,"\n"
ensure
  if( query.kind_of?(SybSQL ) ) then
    print "\n=============  Roll Back =================\n"
    query.sql_norow("rollback tran")
    query.close 
    query = nil
  end
end
