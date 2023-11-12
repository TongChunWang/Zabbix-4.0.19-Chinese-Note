/* Getopt for GNU.
   NOTE: getopt is now part of the C library, so if you don't know what
   "Keep this file name-space clean" means, talk to roland@gnu.ai.mit.edu
   before changing it!
/******************************************************************************
 * *
 *这个代码块的主要目的是实现一个类似于Unix getopt的C语言函数，但具有更灵活的参数顺序处理能力。该函数允许选项与其他参数混合，并在需要时交换选项和非选项的顺序。这个实现基于GNU库，并为Zabbix监控系统定制。
 ******************************************************************************/
/* 版权声明：1987、1988、1989、1990、1991、1992、1993年的Free Software Foundation，Inc.
        此程序是免费软件；您可以根据GNU通用公共许可证（版本2或更高版本）重新分发和/或修改它。
        此程序分发的目的是希望它对您有用，但 without 任何保证；甚至没有暗示的商品质量或特定用途的适应性。
        如需详细了解，请参阅GNU通用公共许可证。
        您应该已收到与此程序一起的GNU通用公共许可证。如果没有，请致信Free Software Foundation，
         地址：51 Franklin Street，Fifth Floor，Boston，MA 02110-1301，USA。
*/

#include "common.h"

/* 如果定义了GETOPT_COMPAT，那么'+'以及'--'都可以作为长选项的引导符。
   由于这不符合POSIX.2标准，因此正在逐步淘汰。
*/
/* #define GETOPT_COMPAT */
#undef GETOPT_COMPAT

/* 这个版本的getopt看起来像标准的Unix getopt，但对用户来说有所不同，
   因为它允许用户在选项和其他参数之间穿插选项。

   当getopt工作时，它会遍历ARGV，将所有选项放在前面，
   然后将所有其他元素放在后面。这样，所有应用程序都可以扩展
    以处理灵活的参数顺序。

   当环境变量POSIXLY_CORRECT设置时，禁用重排。
     此时，行为完全符合标准。

   GNU应用程序可以采用第三种替代模式，以便区分选项和其它
   参数的相对顺序。
*/

#include "zbxgetopt.h"

#undef BAD_OPTION

/* 用于在getopt和调用者之间传递信息的结构体。
   当getopt找到一个需要参数的选项时，该参数的值将返回此处。
   此外，在按顺序返回时，每个非选项ARGV元素都会返回此处。
*/

char *zbx_optarg = NULL;

/* 在ARGV中下一个要扫描的元素的索引。
   此索引用于在调用者和getopt之间以及连续调用getopt时进行通信。

   进入getopt时，索引设置为0，表示第一次调用；进行初始化。

   当getopt返回EOF时，此索引表示第一个非选项ARGV元素的位置，
   此后调用者应自行处理。

   否则，zbx_optind在一次调用之间传递，表示已扫描的ARGV元素数量。
*/

int zbx_optind = 0;

/* 上一次找到选项后的下一个字符，以便从中继续扫描。
   如果此值为0或空字符串，则表示从下一个ARGV元素开始扫描。
*/

static char *nextchar;

/* 调用者在此处存储0，以禁用错误消息
   对于未识别的选项。
*/

int zbx_opterr = 1;

/* 设置为未识别的选项字符。
   此字段必须初始化，以避免在链接时包含系统自身的getopt实现。
*/

#define BAD_OPTION '\0'
int zbx_optopt = BAD_OPTION;

/* 描述如何处理跟随非选项ARGV元素的选项。

   如果调用者没有指定任何内容，
   则默认值为REQUIRE_ORDER（如果环境变量POSIXLY_CORRECT定义），
   否则为PERMUTE。

   REQUIRE_ORDER表示不识别这些选项；
   在看到第一个非选项ARGV元素时，停止处理选项。
   这是Unix的做法。
   要启用此操作模式，可以设置环境变量POSIXLY_CORRECT，
   或使用'+'作为选项字符列表的第一个字符。

   PERMUTE是默认值。我们会遍历ARGV，
   使选项和non-options最终出现在末尾。
   这允许选项以任何顺序给出，
   即使程序未编写为此做好准备也可以适应。

   RETURN_IN_ORDER是针对编写有秩序
   适应选项和其他ARGV元素顺序的程序的选项。
   通过将'-'作为选项字符列表的第一个字符，可以启用此模式。

   当使用--时，无论ordering的值如何，都会强制结束选项解析。
   在RETURN_IN_ORDER模式下，只有--才能使getopt返回EOF，
   且zbx_optind不等于ARGC。
*/

static enum
{
  REQUIRE_ORDER, PERMUTE, RETURN_IN_ORDER
} ordering;

/* 处理选项与顺序的相关变量。

   描述下一个ARGV元素是否为非选项。
   `first_nonopt`是第一个非选项的索引，`last_nonopt`是最后一个非选项的索引。
*/

static int first_nonopt;
static int last_nonopt;

/* 交换两个相邻的ARGV子序列。
   一个子序列包含所有已跳过的非选项（索引[first_nonopt, zbx_optind）
   另一个包含自上次调用getopt以来处理的所有选项。

   `first_nonopt`和`last_nonopt`在交换后重新定位，
   以描述新的非选项在ARGV中的位置。

   要执行交换，我们首先反转所有元素。
   这样，所有选项在所有非选项之前，但顺序错误。

   然后将选项和非选项放回原序。例如：
       原始输入：      a b c -x -y
       反转所有：        -y -x c b a
       反转选项：        -x -y c b a
       反转非选项：      -x -y a b c
*/

static void exchange (char **argv)
{
  char *temp; char **first, **last;

  /* 反转所有[first_nonopt, zbx_optind)范围内的元素 */
  first = &argv[first_nonopt];
  last  = &argv[zbx_optind-1];
  while (first < last) {
    temp = *first; *first = *last; *last = temp; first++; last--;
  }
  /* 放置选项的顺序 */
  first = &argv[first_nonopt];
  first_nonopt += (zbx_optind - last_nonopt);
  last  = &argv[first_nonopt - 1];
  while (first < last) {
    temp = *first; *first = *last; *last = temp; first++; last--;
  }

  /* 放置非选项的顺序 */
  first = &argv[first_nonopt];
  last_nonopt = zbx_optind;
  last  = &argv[last_nonopt-1];
  while (first < last) {
    temp = *first; *first = *last; *last = temp; first++; last--;
  }
}


/* Scan elements of ARGV (whose length is ARGC) for option characters
   given in OPTSTRING.

   If an element of ARGV starts with '-', and is not exactly "-" or "--",
   then it is an option element.  The characters of this element
   (aside from the initial '-') are option characters.  If `getopt'
   is called repeatedly, it returns successively each of the option characters
   from each of the option elements.

   If `getopt' finds another option character, it returns that character,
   updating `zbx_optind' and `nextchar' so that the next call to `getopt' can
   resume the scan with the following option character or ARGV-element.

   If there are no more option characters, `getopt' returns `EOF'.
   Then `zbx_optind' is the index in ARGV of the first ARGV-element
   that is not an option.  (The ARGV-elements have been permuted
   so that those that are not options now come last.)

   OPTSTRING is a string containing the legitimate option characters.
   If an option character is seen that is not listed in OPTSTRING,
   return BAD_OPTION after printing an error message.  If you set `zbx_opterr' to
   zero, the error message is suppressed but we still return BAD_OPTION.

   If a char in OPTSTRING is followed by a colon, that means it wants an arg,
   so the following text in the same ARGV-element, or the text of the following
   ARGV-element, is returned in `zbx_optarg'.  Two colons mean an option that
   wants an optional arg; if there is text in the current ARGV-element,
   it is returned in `zbx_optarg', otherwise `zbx_optarg' is set to zero.

   If OPTSTRING starts with `-' or `+', it requests different methods of
   handling the non-option ARGV-elements.
   See the comments about RETURN_IN_ORDER and REQUIRE_ORDER, above.

   Long-named options begin with `--' instead of `-'.
   Their names may be abbreviated as long as the abbreviation is unique
   or is an exact match for some defined option.  If they have an
   argument, it follows the option name in the same ARGV-element, separated
   from the option name by a `=', or else the in next ARGV-element.
   When `getopt' finds a long-named option, it returns 0 if that option's
   `flag' field is non-zero, the value of the option's `val' field
   if the `flag' field is zero.

   LONGOPTS is a vector of `struct zbx_option' terminated by an
   element containing a name which is zero.

   LONGIND returns the index in LONGOPT of the long-named option found.
   It is only valid when a long-named option has been found by the most
   recent call.

   If LONG_ONLY is non-zero, '-' as well as '--' can introduce
   long-named options.  */

static int zbx_getopt_internal (int argc, char **argv, const char *optstring,
                 const struct zbx_option *longopts, int *longind,
                 int long_only)
{
  static char empty_string[1];
  int option_index;

  if (longind != NULL)
    *longind = -1;

  zbx_optarg = 0;

  /* Initialize the internal data when the first call is made.
     Start processing options with ARGV-element 1 (since ARGV-element 0
     is the program name); the sequence of previously skipped
     non-option ARGV-elements is empty.  */

  if (zbx_optind == 0)
    {
      first_nonopt = last_nonopt = zbx_optind = 1;

      nextchar = NULL;

      /* Determine how to handle the ordering of options and nonoptions.  */

      if (optstring[0] == '-')
        {
          ordering = RETURN_IN_ORDER;
          ++optstring;
        }
      else if (optstring[0] == '+')
        {
          ordering = REQUIRE_ORDER;
          ++optstring;
        }
#if OFF
      else if (getenv ("POSIXLY_CORRECT") != NULL)
        ordering = REQUIRE_ORDER;
#endif
      else
        ordering = PERMUTE;
    }

  if (nextchar == NULL || *nextchar == '\0')
    {
      if (ordering == PERMUTE)
        {
          /* If we have just processed some options following some non-options,
             exchange them so that the options come first.  */

          if (first_nonopt != last_nonopt && last_nonopt != zbx_optind)
            exchange (argv);
          else if (last_nonopt != zbx_optind)
            first_nonopt = zbx_optind;

          /* Now skip any additional non-options
             and extend the range of non-options previously skipped.  */

          while (zbx_optind < argc
                 && (argv[zbx_optind][0] != '-' || argv[zbx_optind][1] == '\0')
#ifdef GETOPT_COMPAT
                 && (longopts == NULL
                     || argv[zbx_optind][0] != '+' || argv[zbx_optind][1] == '\0')
#endif                          /* GETOPT_COMPAT */
                 )
            zbx_optind++;
          last_nonopt = zbx_optind;
        }

      /* Special ARGV-element `--' means premature end of options.
         Skip it like a null option,
         then exchange with previous non-options as if it were an option,
         then skip everything else like a non-option.  */

      if (zbx_optind != argc && !strcmp (argv[zbx_optind], "--"))
        {
          zbx_optind++;

          if (first_nonopt != last_nonopt && last_nonopt != zbx_optind)
            exchange (argv);
          else if (first_nonopt == last_nonopt)
            first_nonopt = zbx_optind;
          last_nonopt = argc;

          zbx_optind = argc;
        }

      /* If we have done all the ARGV-elements, stop the scan
         and back over any non-options that we skipped and permuted.  */

      if (zbx_optind == argc)
        {
          /* Set the next-arg-index to point at the non-options
             that we previously skipped, so the caller will digest them.  */
          if (first_nonopt != last_nonopt)
            zbx_optind = first_nonopt;
          return EOF;
        }

      /* If we have come to a non-option and did not permute it,
         either stop the scan or describe it to the caller and pass it by.  */

      if ((argv[zbx_optind][0] != '-' || argv[zbx_optind][1] == '\0')
#ifdef GETOPT_COMPAT
          && (longopts == NULL
              || argv[zbx_optind][0] != '+' || argv[zbx_optind][1] == '\0')
#endif                          /* GETOPT_COMPAT */
          )
        {
          if (ordering == REQUIRE_ORDER)
            return EOF;
          zbx_optarg = argv[zbx_optind++];
          return 1;
        }

      /* We have found another option-ARGV-element.
         Start decoding its characters.  */

      nextchar = (argv[zbx_optind] + 1
                  + (longopts != NULL && argv[zbx_optind][1] == '-'));
    }

  if (longopts != NULL
      && ((argv[zbx_optind][0] == '-'
           && (argv[zbx_optind][1] == '-' || long_only))
#ifdef GETOPT_COMPAT
          || argv[zbx_optind][0] == '+'
#endif                          /* GETOPT_COMPAT */
          ))
    {
      const struct zbx_option *p;
      char *s = nextchar;
      int exact = 0;
      int ambig = 0;
      const struct zbx_option *pfound = NULL;
      int indfound = 0;
      int needexact = 0;

#if ON
      /* allow `--option#value' because you cannot assign a '='
         to an environment variable under DOS command.com */
      while (*s && *s != '=' && * s != '#')
        s++;
#else
      while (*s && *s != '=')
        s++;
#endif

      /* Test all options for either exact match or abbreviated matches.  */
      for (p = longopts, option_index = 0; p->name;
           p++, option_index++)
        if (!strncmp (p->name, nextchar, (unsigned) (s - nextchar)))
          {
            if (p->has_arg & 0x10)
              needexact = 1;
            if ((unsigned) (s - nextchar) == strlen (p->name))
              {
                /* Exact match found.  */
                pfound = p;
                indfound = option_index;
                exact = 1;
                break;
              }
#if OFF	/* ZBX: disable long option partial matching */
            else if (pfound == NULL)
              {
                /* First nonexact match found.  */
                pfound = p;
                indfound = option_index;
              }
            else
              /* Second nonexact match found.  */
              ambig = 1;
#endif
          }

      /* don't allow nonexact longoptions */
      if (needexact && !exact)
        {
          if (zbx_opterr)
                zbx_error("unrecognized option `%s'", argv[zbx_optind]);

          nextchar += strlen (nextchar);
          zbx_optind++;
          return BAD_OPTION;
        }
#if OFF	/* disable since ambig is always 0*/
      if (ambig && !exact)
        {
          if (zbx_opterr)
                zbx_error("option `%s' is ambiguous", argv[zbx_optind]);

          nextchar += strlen (nextchar);
          zbx_optind++;
          return BAD_OPTION;
        }
#endif

      if (pfound != NULL)
        {
          int have_arg = (s[0] != '\0');
          if (have_arg && (pfound->has_arg & 0xf))
            have_arg = (s[1] != '\0');
          option_index = indfound;
          zbx_optind++;
          if (have_arg)
            {
              /* Don't test has_arg with >, because some C compilers don't
                 allow it to be used on enums.  */
              if (pfound->has_arg & 0xf)
                zbx_optarg = s + 1;
              else
                {
                  if (zbx_opterr)
                    {
                      if (argv[zbx_optind - 1][1] == '-')
                        /* --option */
                        zbx_error("option `--%s' doesn't allow an argument",pfound->name);
                      else
                        /* +option or -option */
                        zbx_error("option `%c%s' doesn't allow an argument", argv[zbx_optind - 1][0], pfound->name);
                    }
                  nextchar += strlen (nextchar);
                  return BAD_OPTION;
                }
            }
          else if ((pfound->has_arg & 0xf) == 1)
            {
#if OFF
              if (zbx_optind < argc)
#else
              if (zbx_optind < argc && (pfound->has_arg & 0x20) == 0)
#endif
                zbx_optarg = argv[zbx_optind++];
              else
                {
                  if (zbx_opterr)
                    zbx_error("option `--%s%s' requires an argument",
                             pfound->name, (pfound->has_arg & 0x20) ? "=" : "");
                  nextchar += strlen (nextchar);
                  return optstring[0] == ':' ? ':' : BAD_OPTION;
                }
            }
          nextchar += strlen (nextchar);
          if (longind != NULL)
            *longind = option_index;
          if (pfound->flag)
            {
              *(pfound->flag) = pfound->val;
              return 0;
            }
          return pfound->val;
        }
      /* Can't find it as a long option.  If this is not getopt_long_only,
         or the option starts with '--' or is not a valid short
         option, then it's an error.
         Otherwise interpret it as a short option.  */
      if (!long_only || argv[zbx_optind][1] == '-'
#ifdef GETOPT_COMPAT
          || argv[zbx_optind][0] == '+'
#endif                          /* GETOPT_COMPAT */
          || strchr (optstring, *nextchar) == NULL)
        {
          if (zbx_opterr)
            {
              if (argv[zbx_optind][1] == '-')
                /* --option */
                zbx_error("unrecognized option `--%s'", nextchar);
              else
                /* +option or -option */
                zbx_error("unrecognized option `%c%s'", argv[zbx_optind][0], nextchar);
            }
          nextchar = empty_string;
          zbx_optind++;
          return BAD_OPTION;
        }
        (void) &ambig;  /* UNUSED */
    }

  /* Look at and handle the next option-character.  */

  {
    char c = *nextchar++;
    const char *temp = strchr (optstring, c);

    /* Increment `zbx_optind' when we start to process its last character.  */
    if (*nextchar == '\0')
      ++zbx_optind;

    if (temp == NULL || c == ':')
      {
        if (zbx_opterr)
          {
#if OFF
            if (c < 040 || c >= 0177)
              zbx_error("unrecognized option, character code 0%o", c);
            else
              zbx_error("unrecognized option `-%c'", c);
#else
            /* 1003.2 specifies the format of this message.  */
            zbx_error("invalid option -- %c", c);
#endif
          }
        zbx_optopt = c;
        return BAD_OPTION;
      }
    if (temp[1] == ':')
      {
        if (temp[2] == ':')
          {
            /* This is an option that accepts an argument optionally.  */
            if (*nextchar != '\0')
              {
                zbx_optarg = nextchar;
                zbx_optind++;
              }
            else
              zbx_optarg = 0;
            nextchar = NULL;
          }
        else
          {
            /* This is an option that requires an argument.  */
            if (*nextchar != '\0')
              {
                zbx_optarg = nextchar;
                /* If we end this ARGV-element by taking the rest as an arg,
                   we must advance to the next element now.  */
                zbx_optind++;
              }
            else if (zbx_optind == argc)
              {
                if (zbx_opterr)
                  {
#if OFF
                    zbx_error("option `-%c' requires an argument", c);
#else
                    /* 1003.2 specifies the format of this message.  */
                    zbx_error("option requires an argument -- %c", c);
#endif
                  }
                zbx_optopt = c;
                if (optstring[0] == ':')
                  c = ':';
                else
                  c = BAD_OPTION;
              }
            else
              /* We already incremented `zbx_optind' once;
                 increment it again when taking next ARGV-elt as argument.  */
              zbx_optarg = argv[zbx_optind++];
            nextchar = NULL;
          }
      }
    return c;
  }
}

int zbx_getopt(int argc, char **argv, const char *optstring)
{
  return zbx_getopt_internal (argc, argv, optstring,
                           (const struct zbx_option *) 0,
                           (int *) 0,
                           0);
}
// int *opt_index：指向长选项索引的指针。
int zbx_getopt_long(int argc, char **argv, const char *options,
                    const struct zbx_option *long_options, int *opt_index)
{
  // 调用zbx_getopt_internal函数，该函数内部实现获取命令行参数的功能。
  // 传入的参数如下：
  // int argc：命令行参数个数；
  // char **argv：指向命令行参数的指针数组；
  // const char *options：选项字符串，用于匹配命令行参数；
  // const struct zbx_option *long_options：长选项指针数组；
  // int *opt_index：指向长选项索引的指针。
  // 返回值为zbx_getopt_internal函数的返回值。

  return zbx_getopt_internal (argc, argv, options, long_options, opt_index, 0);
}



#ifdef TEST2

/* Compile with -DTEST to make an executable for use in testing
   the above definition of `getopt'.  */
/******************************************************************************
 * 
 ******************************************************************************/
/*
 * 这个程序的主要目的是解析命令行参数，根据不同的参数值执行相应的操作。
 * 输出结果包括：
 * 1. 数字参数值的出现位置
 * 2. 字母a和b选项的出现位置
 * 3. 字母c选项的值
 * 4. 非选项参数的值
 */

int
main (argc, argv)
     int argc;
     char **argv;
{
  // 定义一个整型变量c，用于存储解析后的选项参数
  int c;
  // 定义一个整型变量digit_optind，用于存储数字选项的出现位置
  int digit_optind = 0;

  // 使用一个无限循环来处理命令行参数
  while (1)
    {
      // 定义一个整型变量this_option_optind，用于存储当前选项的位置
      int this_option_optind = zbx_optind ? zbx_optind : 1;

      // 从命令行参数中获取一个选项
      c = getopt (argc, argv, "abc:d:0123456789");
      // 如果到达了选项的末尾，跳出循环
      if (c == EOF)
        break;

      // 根据选项c的值进行切换操作
      switch (c)
        {
        // 处理数字选项（0-9）
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
          // 如果数字选项的出现位置不是第一个，或者与当前位置不同，输出提示信息
          if (digit_optind != 0 && digit_optind != this_option_optind)
            printf ("digits occur in two different argv-elements.\n");
          // 更新数字选项的出现位置
          digit_optind = this_option_optind;
          // 输出选项字母和其值
          printf ("option %c\n", c);
          break;

        // 处理字母a和b选项
        case 'a':
        case 'b':
          // 输出选项字母
          printf ("option b\n");
          break;

        // 处理字母c选项，并输出其值
        case 'c':
          printf ("option c with value `%s'\n", zbx_optarg);
          break;

        // 处理无效选项
        case BAD_OPTION:
          break;

        // 处理其他未知选项，输出错误信息
        default:
          printf ("?? getopt returned character code 0%o ??\n", c);
        }
    }

  // 如果有非选项参数，输出其值
  if (zbx_optind < argc)
    {
      printf ("non-option ARGV-elements: ");
      while (zbx_optind < argc)
        printf ("%s ", argv[zbx_optind++]);
      printf ("\n");
    }

  // 程序正常退出
  exit (0);
}


#endif /* TEST */
