#! /usr/local/bin/ruby
# sample script ,(Execute 2 SQL commands)
# ex. 
#   $ LANG=ja_JP.ujis ; export LANG
#   $ ruby  sqlsample.rb -S SYBASE -U sa -P XXXXXX 

require "sybsql.rb"

# Redefinition of callbacks
class MyContext < SybSQLContext

  # redefine defualt callback
  def srvmsgCB( con,msghash)
    return true unless ( msghash.kind_of?(Hash) )
#    if (( msghash[ "severity" ] == 0) || ( msghash[ "severity" ] == 10)) then
#      return true
#    end
    
    $stderr.print "\n** MyContext Server Message: **\n"
    $stderr.print "  Message number #{msghash['msgnumber']} ",
    "Severity #{msghash['severity']} ",
    "State #{msghash['state']} ", "Line #{msghash['line']} \n"
    
    $stderr.print "  Server #{msghash['srvname']}\n"
    $stderr.print "  Procedure #{msghash['proc']}\n"
    $stderr.print "  Message String:  #{msghash['text']}\n"

    return true
  end

  def cltmsgCB( con,msghash)
    # 
    return true unless ( msghash.kind_of?(Hash) )
#    unless ( msghash[ "severity" ] ) then
#      return true
#    end
    
    $stderr.print "\n** MyContext Client-Message: **\n"

    $stderr.print "  Message number: ",
      "LAYER=#{msghash[ 'layer' ]} ", "ORIGIN=#{msghash[ 'origin' ]} ",
    "SEVERITY=#{msghash[ 'severity' ]} ", "NUMBER=#{msghash[ 'number' ]}\n"
    
    $stderr.print "  Message String: #{msghash['msgstring']}\n"
    $stderr.print "  OS Error: #{msghash['osstring']}\n"

    # Not retry , CS_CV_RETRY_FAIL( probability TimeOut ) 
    if( msghash[ 'severity' ] == "RETRY_FAIL" ) then
      @timeout_p = true
      return false
    end
    return true
  end
end


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
		     'appname'=>'rubysample','lang'=>lang},
		   MyContext
		 )
  
  # use db
  unless( query.sql_norow("use master") ) then
    raise "failed, use master"
  end

  # set  ROW_COUNT 10
  query.set_rowcount(10)

  # Execute 2 SQL commands
  query.sql("select * from sysdatabases\n select * from systypes")
  # CMD_DONE exists?
  raise "failed, SELECT" unless (query.cmd_done? )

  # first ROWRESULT 
  print "\nselect * from sysdatabases (first 10 row)\n"
  res = query.top_row_result
  print "  ", res.columns.join('|'), "\n"
  print "  ------------------------------------------\n"
  res.rows.each {
    |r| print "  #{r.join('|')}\n"
  }

  # second ROWRESULT 
  print "\nselect * from systypes  (first 10 row)\n"
  unless (res = query.nth_results(1, CS_ROW_RESULT) )then
    raise "No secound result"
  end
  print "  ", res.columns.join('|'), "\n"
  print "  ------------------------------------------\n"
  res.rows.each {
    |r| print "  #{r.join('|')}\n"
  }

  print "OK\n"

rescue
  # raise
  # print "!! ERROR : ", $!,$@, "\n"
  print "!! ERROR : ", $!,"\n"
ensure
  query.close if(query.kind_of?( SybSQL ) );
  query = nil
end

# print "GC start\n"
# GC.start
# print "END\n"
