#! /usr/local/bin/ruby
#-*- coding: euc-jp -*-
#
#  Copyright (C) 1999  Tetsuhumi Takaishi

=begin

* Description *
  This is a example that looks like a $SYBASE/bin/isql command.

* Usage *
  See, 
   ruby isql.rb -h 

* ReadLine *
  isql.rb uses ReadLine module.

* Tablename completion
   use database-name :
        Table names and procedure names in the DB are registered to
      completion buffer of ReadLine module.

* Column names completion *
  ?table-name :
        Column names in the table are registered to completion buffer.

* Commands except SQL *
  go			Same as isql.
  quit			Quit
  reset			Clear of command buffer
  /			History representation
  ?table-name		Appends column names to completion buffer

* デフォルトでは、フェッチできるローの最大数を 2000 に設定しています
  T-SQLの set rowcount コマンドで変更できます。ただし、 set rowcount 0 
  で無制限に設定すると、巨大ローの検索でメモリーを浪費してしまいます。

* C-c (SIGINT) の実装が poor です。本物のisqlのように素早くキャンセル
 できない筈です。

* Environment variables *
  INPUTRC	The default is ~/.isqlrb.rc 

=end

# DEBUG memo
#      ruby -I ./ isql.rb -J ja_JP.ujis <isqltest.sql

require "readline"
include Readline

require "sybsql.rb"

class IO
  def myprint(*args)
    self.print *args
    if( defined?($Log) && $Log.kind_of?(IO) ) then
      $Log.print *args 
    end
  end
end

module Readline
  MYHIST = [
    "select", "insert", "update", "delete", "drop", "use", "exec", "set",
    "commit", "rollback", "tran", 
    "from", "into", "onto", "where", "count(", "having", "group",
    "option", "rowcount", "forceplan",
    "sp_help", 
  ]
  MYOBJHIST = []
  MYCOLHIST = {}  # HASH! ( for unique Array )
end


Readline.completion_proc = proc {
  |str|
  ret = []
  rstr = '^[\s]*' + str.gsub(/(\W)/){ "\\#{$1}"} + '.*'
  reg = Regexp.new(rstr, true)

  if( str[0,1] == '?' ) then
    MYOBJHIST.each {
      |x|
      xstr = '?' + x
      ret.push(xstr) if( xstr =~ reg )
    }
  else
    MYHIST.each {
      |x|
      ret.push(x) if( x =~ reg )
    }
    MYOBJHIST.each {
      |x|
      ret.push(x) if( x =~ reg )
    }
    MYCOLHIST.each_key {
      |x|
      ret.push(x) if( x =~ reg )
    }
  end
  ret
}

# select SC.name from syscolumns SC,sysobjects SO where SO.name = 'ImageTest' and SC.id  = SO.id
# Gets sybase objects list from sysobjects-table
def objects_list (query)
  begin
    types = "('U','P','V','S')"
    # types = "('U','P','V','S','TR','R')"
    sql = "select name from sysobjects where type in" + types
    query.sql(sql)
    return [] unless (query.cmd_done?)
    res = query.top_row_result
    return [] unless( res.kind_of?( SybResult) )
    return res.rows.flatten
  rescue
    return []
  end
end

def columns_list( query, obj)
  begin
    sql = 'select SC.name from syscolumns SC,sysobjects SO ' +
      "where SO.name = '#{obj}' and SC.id  = SO.id"
    query.sql(sql)
    return [] unless (query.cmd_done?)
    res = query.top_row_result
    return [] unless( res.kind_of?( SybResult) )
    return res.rows.flatten
  rescue
    return []
  end
end

# Redefine srvmsgCN
class SybSQLContext
  def srvmsgCB( con,msghash)
    return true unless ( msghash.kind_of?(Hash) )
    if (( msghash[ "severity" ] == 0) || ( msghash[ "severity" ] == 10)) then
      if( msghash[ 'msgnumber' ] == 5701 ) then
	$DBChange = true
      end
      return true
    end
    
    $stderr.myprint "\n** SybSQLContext Server Message: **\n"
    $stderr.myprint "  Message number #{msghash['msgnumber']} ",
    "Severity #{msghash['severity']} ",
    "State #{msghash['state']} ", "Line #{msghash['line']} \n"
    
    $stderr.myprint "  Server #{msghash['srvname']}\n"
    $stderr.myprint "  Procedure #{msghash['proc']}\n"
    $stderr.myprint "  Message String:  #{msghash['text']}\n"

    return true
  end
end

class PrintResult
  def initialize( res )
    @res = res
    @field_sep = " | "
  end

  # 配列要素の長さの配列を返す
  def element_length( ary )
    ret = []
    ary.each { |e| 
      if( e.is_a?( String) ) then
	ret.push( e.length )
      else
	ret.push( e.to_s.length)
      end
    }
    ret
  end

  def max_lengths_each_cell 
    lens = []
    if( @res.columns )then
      lens = element_length( @res.columns )
    end
    for r in @res.rows
      if( r.kind_of?(Array) ) then
	r_lens = element_length( r )
	for i in ( 0 .. (r.length - 1) )
	  nlen = lens[i]
	  nlen = 0 unless( nlen)
	  lens[i] = r_lens[i] if( r_lens[i] > nlen )
	end
      end
    end
    lens
  end

  MAX_COLUMN_LENGTH = 132
  # pretty print
  def pretty_rows
    lens = max_lengths_each_cell
    if( @res.columns && @res.columns.length > 0 )then
      line = []
      @res.columns.each_index {
	|i|
	len_just = lens[i]
	len_just = MAX_COLUMN_LENGTH if (len_just > MAX_COLUMN_LENGTH)
	line.push( @res.columns[i].to_s.ljust( len_just ) )
      }
      $stdout.myprint line.join(@field_sep),"\n"

      # Print separators for columns and rows
      i = 0
      lens.each { |c| i = i + c }
      i = i + @field_sep.size * (lens.length - 1 )
      i = MAX_COLUMN_LENGTH if (i > MAX_COLUMN_LENGTH)
      $stdout.myprint '-' * i , "\n"
    end

    return unless( @res.rows.is_a?(Array) )

    @res.rows.each {
      |r|
      if( r.is_a?(Array) )then
	line = []
	r.each_index {
	  |i|
	  line.push( r[i].to_s.ljust( lens[i] ) )
	}
	$stdout.myprint line.join(' | '),"\n"
      end
    }
  end

  ASCII_TRANSTATE = {
    CS_TRAN_UNDEFINED => 'undefined',
    CS_TRAN_IN_PROGRESS => 'in progress',
    CS_TRAN_COMPLETED => 'completed',
    CS_TRAN_FAIL => 'failed',
    CS_TRAN_STMT_FAIL => 'failed(STMT_FAIL)',
  }
  def pretty_info
    rcnt = @res.row_count
    if( (rcnt == nil) || (rcnt <= 0 ) )then
      rcnt = 0
    end
    tstate = ASCII_TRANSTATE[ @res.tran_state ]
    if( rcnt || tstate ) then
      $stdout.myprint "(row count = #{rcnt}, tran state = '#{tstate}')\n"
    end
  end
end

def help
  $stdout.myprint <<HELPEND
go		Exec SQL
quit		QUIT isql.rb
reset		Clear command buffer
/		Show history
R ruby-command	Eval ruby 
?		Show this message
?table-name	Show table column
HELPEND
end

class ClearSQL<Exception 
end

def getpsw
  require "curses"
  psw = String.new("")
  begin
    # include Curses
    Curses.init_screen
    Curses.clear
    Curses.nonl
    Curses.cbreak
    Curses.noecho
    Curses.addstr("Password: ")
    while( true )
      c = Curses.getch
      #break if( c == ?\n )
      break if( c == 10 )
      #break if( c == ?\r )
      break if( c == 13 )
      psw.concat(c)
      Curses.addch(?*)
    end
    return psw
  ensure
    Curses.close_screen
  end
end


$lang = ENV['LANG']
$lang = 'default' unless $lang

ENV['INPUTRC'] = "~/.isqlrb.rc"

$server = "SYBASE"
$user = "sa"
$psw = nil
$timeout = 0
$maxrows = 2000
$appname = "isql.rb"
$logfile = nil

while ( !ARGV.empty? )	
    case ARGV.shift
    when /^-S$/, /^-S(.*)$/
      $server = $1 || ARGV.shift
    when /^-U$/, /^-U(.*)$/
      $user = $1 || ARGV.shift
    when /^-P$/, /^-P(.*)$/
      $psw = $1 || ARGV.shift
    when /^-J$/, /^-J(.*)$/
      $lang = $1 || ARGV.shift
    when /^-A$/, /^-A(.*)$/
      $appname = $1 || ARGV.shift
    when /^-L$/, /^-L(.*)$/
      $logfile = $1 || ARGV.shift
    when /^-t$/, /^-t(.*)$/
      $timeout = $1 || ARGV.shift
      $timeout = $timeout.to_i
    else
      $stdout.myprint <<USAGE
usage : #{$0} [-h] [-S server(default SYBASE)] [-U username(default sa)]
            [-P password]  [-t timeout(default 0)] [-J lang(default \$LANG)]
            [-A app-name(default isql.rb)] [-L log-filename]
USAGE
      exit 1
    end
end

$psw = getpsw if( $psw.nil? )

$Log = nil
if( $logfile)
  begin
    $Log = File.open($logfile,"a")
  rescue
    $Log = nil
    $stderr.print "\nERROR log open: ", $!,"\n"
  end
end

if( $DEBUG ) then
  $stdout.print [$server,$user,$psw,$lang,$appname,$timeout],"\n"
end
  
def open_dbsrv( verbose = false)
  $Query=SybSQL.new({'S'=>$server,'U'=>$user, 'P'=>$psw, 'timeout' => $timeout,
		     'appname'=>$appname, 'lang'=>$lang} )
  # initialize connection
  $Query.set_strip(true)  # 
  $Query.fetch_rowfail(true) # fetch CS_ROW_FAIL 
  if( $Query.set_rowcount($maxrows)) then	# set rowcount
    if verbose then
      $stdout.myprint "\n!! MAX ROWCOUNT #{$maxrows} !!\n\n"
    end
  end
end

$DBChange = false
begin
  # open db server
  open_dbsrv(true)

  sql = ""
  linecnt = 1
  Readline.completion_case_fold=(true)
  trap( "SIGINT" ) { raise ClearSQL } # poor implementation 

  while(true)
    begin
      cmd = readline("#{linecnt}-> ", false)
      if (cmd.nil?) then
	raise EOFError
      end
      
      case cmd
      when /^\s*go\s*$/io	# Exec SQL command
	next if( sql.length <= 0 )
	unless( sql =~ /\S+/o ) then
	  raise ClearSQL,""
	end

	# Add history
	HISTORY.push(sql)

	$stdout.myprint "<- #{sql}\n"
	unless( $Query.sql(sql).kind_of?(Array) )
	  $stdout.myprint "SQL Failed\n"
	  raise ClearSQL,""
	end
	for res in $Query.results
	  next unless( res.kind_of?( SybResult ) )
	  case res.restype
	  when CS_CMD_SUCCEED
	    $stdout.myprint "SUCCEED\n"
	    # PrintResult.new(res).pretty_info
	  when CS_CMD_DONE
	    # $stdout.myprint "DONE\n"
	    PrintResult.new(res).pretty_info
	  when CS_CMD_FAIL
	    $stdout.myprint "FAIL\n"
	    PrintResult.new(res).pretty_info
	  when CS_ROW_RESULT, CS_PARAM_RESULT, CS_STATUS_RESULT,CS_COMPUTE_RESULT
	    s_restype = SybResult::ASCII_RESTYPE[res.restype]
	    s_restype = "UnKnown-Type-#{res.restype}" unless(s_restype)
	    $stdout.myprint "restype =", s_restype, "\n"
	    # load "mydebug.rb"
	    PrintResult.new(res).pretty_rows
	  else
	    $stdout.myprint "unknown restype\n"
	  end
	  $stdout.myprint "\n"
	end
	if( $DBChange ) then
	  # Data base change, MYOBJHIST refresh!, MYCOLHIST clear
	  $stdout.myprint "DB change\n"	if( $DEBUG)
	  MYCOLHIST.clear
	  MYOBJHIST.clear
	  MYOBJHIST.concat( objects_list($Query) )
	  $DBChange = false
	end
	raise ClearSQL,""

      when /^\s*quit\s*$/io	# Quit
	raise EOFError

      when /^\s*reset\s*$/io	# Clear command buffer
	raise ClearSQL,""

      when /^\s*\/\s*$/o	# Show history
	# print history
	HISTORY.each{ |h| $stdout.myprint h,"\n" }
      when /^\s*R\s+/o		# Evale Ruby
	# eval ruby
	cmd.sub!( /^\s*R\s+/o, "")
	begin
	  $stdout.myprint cmd, "\n"
	  $stdout.myprint eval(cmd),"\n"
	rescue
	  $stdout.myprint "RYBY ERROR: #{$!}\n"
	end
      when /^\s*\?\s*$/o	# Help command
	help
      when /^\s*\?(\S+)\s*$/o	# Help objects
	# Update MYCOLHIST
	clist = columns_list($Query, $1)
	$stdout.myprint " ",clist.join(','),"\n"
	clist.each{ |v| MYCOLHIST[ v ] = true }
      else
	linecnt += 1
	sql = sql + " " if( sql != "" )
	sql = sql + cmd
      end
      
    rescue SybTimeOutError
      open_dbsrv
      sql = ""
      linecnt = 1
    rescue ClearSQL
      $stdout.myprint "Catch ClearSQL ", $!, "\n" if ($DEBUG)
      sql = ""
      linecnt = 1
    end
  end	# End of readline loop

rescue EOFError
  $stdout.myprint "EOF\n"
rescue
  $stdout.myprint "ERROR: ", $!,"\n"
  # debug
  if( $@.kind_of?(Array) )then
    $@.each{ |s| $stdout.myprint "  ",s,"\n" }
  end
ensure
  if( defined?($Query) ) then
    $Query.close if($Query.kind_of?( SybSQL ) );
    $Query = nil
  end
end
