using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace ns_type_parser_csharp_backend
{
  class type_parser_csharp_backend
  {
    class Type
    {
      public string abTypeName; /* type name */
      public string abMemberName; /* for members of structs, enums, unions */
      public int iSize;
      public int iAlignment;

      public enum Kind
      {
        SIMPLE = 0,   /* simple types don't have children, interpretation of data representation is by type name */
        STRUCT = 1,   /* children specify structure members */
        UNION = 2,    /* children speciy union members */
        ENUM = 3,   /* children specify enum mebers */
        ARRAY = 4,    /* for arrays we denote the element base type in atChildren[0] */
      };

      public Kind eKind;

      public bool fIsConstValue;
      public Int64 iConstValue; /* for enum constants */
      public int numChildren; /* for record types */
      public List<Type> atChildren;
      public Type parent;
    }

    class Define
    {
      public string abName;
      public string abValue;
    }


    static List<Type> typeList = new List<Type>();
    static List<Define> defineList = new List<Define>();

    static string readString(System.IO.BinaryReader br)
    {
      string ret = "";
      char c;
      while ((c = (char)br.ReadByte()) != 0)
        ret += c;
      return ret;
    }

    static void deserialize_packet(System.IO.BinaryReader br, Type parent)
    {
      Type t = new Type();
      t.parent = parent;
      t.abMemberName = readString(br);
      t.abTypeName = readString(br);
      t.eKind = (Type.Kind)br.ReadInt32();
      t.iSize = br.ReadInt32();
      t.iAlignment = br.ReadInt32();
      t.fIsConstValue = (br.ReadInt32() != 0);
      t.iConstValue = br.ReadInt64();
      t.numChildren = br.ReadInt32();
      t.atChildren = new List<Type>();

      for (int i = 0; i < t.numChildren; i++)
        deserialize_packet(br, t);

      if (parent != null)
        parent.atChildren.Add(t);
      else
        typeList.Add(t);
    }

    private static bool loadPacketDump(string file)
    {
      try
      {
        System.IO.FileStream fs = new System.IO.FileStream(file, System.IO.FileMode.Open);
        System.IO.BinaryReader br = new System.IO.BinaryReader(fs);
        UInt32 magic = br.ReadUInt32();
        if (magic != 0x23c0ffee)
        {
          System.Console.WriteLine("This is not a valid packet dump");
          return false;
        }
        UInt32 numTypes = br.ReadUInt32();
        for (int i = 0; i < numTypes; i++)
          deserialize_packet(br, null);

        UInt32 magic2 = br.ReadUInt32();
        if (magic2 != 0x12021984)
        {
          System.Console.WriteLine("This is not a valid packet dump");
          return false;
        }
        UInt32 numDefines = br.ReadUInt32();
        for (int i = 0; i < numDefines; i++)
        {
          Define d = new Define();
          d.abName = readString(br);
          d.abValue = readString(br);
          defineList.Add(d);
        }

        br.Close();
      }
      catch (Exception)
      {
        return false;
      }

      return true;
    }


    static void _indent(int indent)
    {
      for (int i = 0; i < indent; i++)
      {
        System.Console.Write(" ");
      }
    }

    /* must be consistent with mappedBaseTypes in ordering and length */
    static string[] baseTypes =
    {
        "Pointer",
        "Char32",
        "UInt",
        "Long",
        "Int",
        "Char16",
        "WChar",
        "UShort",
        "Short",
        "SChar",
        "Char_S",
        "UChar",
        "LongLong",
        "ULongLong",
        "Bool",
        "UInt128",
        "Float",
        "Double",
        "LongDouble",
    };

    /* must be consistent with baseTypes in ordering and length */
    static string[] mappedBaseTypes =
    {
        "IntPtr",
        "uint",
        "uint",
        "int",
        "int",
        "ushort",
        "ushort",
        "ushort",
        "short",
        "short",
        "short",
        "byte",
        "long",
        "ulong",
        "bool",
        "UInt128?",
        "float",
        "double",
        "LongDouble?",
    };

    static bool isBaseType(Type t)
    {
      foreach (string s in baseTypes)
        if (t.abTypeName == s)
          return true;
      return false;
    }

    static string mapBaseType(string typeName)
    {
      for (int i = 0; i < baseTypes.Length; i++)
        if (baseTypes[i] == typeName)
          return mappedBaseTypes[i];
      return null;
    }
  

    static Type FindPrimitiveType(Type type)
    {
      foreach (Type t in typeList)
      {
        if (t.abMemberName == type.abTypeName)
          return FindPrimitiveType(t);
      }
      return type;
    }

    static Type FindType(string abTypeName)
    {
      foreach (Type t in typeList)
      {
        if (t.abTypeName == abTypeName)
          return t;
      }
      return null;
    }

    static void dump_type(Type type, int indent)
    {
      switch (type.eKind)
      {
        case Type.Kind.STRUCT:

          if (type.iAlignment == 1)
          {
            _indent(indent);
            System.Console.WriteLine("[StructLayout(LayoutKind.Sequential)]");
          }

          _indent(indent);
          System.Console.WriteLine("public struct " + type.abMemberName + "\n{");
          foreach (Type c in type.atChildren)
          {
            dump_type(c, indent + 2);
          }
          _indent(indent);
          System.Console.WriteLine("}\n");
          break;

        case Type.Kind.ARRAY:
          {
            _indent(indent);
            System.Console.WriteLine("[MarshalAs(UnmanagedType.ByValArray, SizeConst = " + type.iSize + ")]");
            _indent(indent);
            Type t = FindPrimitiveType(type.atChildren[0]);

            string mappedBaseType = mapBaseType(t.abTypeName);
            if (mappedBaseType != null)
            {
              System.Console.WriteLine(String.Format("public {0}[] {1};", mappedBaseType, type.abMemberName));
            }
            else
            {
              System.Console.Write("public " + t.abMemberName + "[] ");
              System.Console.WriteLine(type.abMemberName + ";");
            }
          }
          break;

        case Type.Kind.SIMPLE:
          {
            Type t = FindPrimitiveType(type);
            _indent(indent);

            if (!isBaseType(type))
            {
              string mappedBaseType = mapBaseType(t.abTypeName);

              if (mappedBaseType != null)
              {
                System.Console.WriteLine(String.Format("public {0} {1};", mappedBaseType, type.abMemberName));
              }
              else
              {
                if (FindType(type.abTypeName) != null)
                {
                  System.Console.WriteLine("/* !!! FIXME: Type " + type.abMemberName + " seems to be a simple typedef (alias of " + type.abTypeName + ") for which there is not equivalent in C#. */");
                }
                else
                {
                  System.Console.Write("public " + type.abTypeName + " ");
                  System.Console.WriteLine(type.abMemberName + ";");
                }
              }
            }
            break;
          }
        case Type.Kind.ENUM:
          System.Console.WriteLine("public enum " + type.abMemberName + "\n{");
          foreach (Type c in type.atChildren)
          {
            _indent(indent + 2);
            System.Console.WriteLine(c.abMemberName + " = " + c.iConstValue + ",");
          }
          _indent(indent);
          System.Console.WriteLine("}\n");
          
          break;
        case Type.Kind.UNION:
          _indent(indent);
          System.Console.WriteLine("/* !!! FIXME: Union type is not supported in C# ! Skipping union " + type.abMemberName + ". */");
          break;
        default:
          break;
      }
    }

    static void Main(string[] args)
    {
      if (args.Length != 1)
      {
        System.Console.WriteLine("Usage: this_program.exe <input file as created by frontend>.bin");
        return;
      }

      if (!System.IO.File.Exists(args[0]))
      {
        System.Console.WriteLine("File not found");
        return;
      }
      
      if (!loadPacketDump(args[0]))
      {
        System.Console.WriteLine("Failed to load packet dump");
        return;
      }

      foreach (Define d in defineList)
      {
        System.Console.WriteLine("public const uint " + d.abName + " = " + d.abValue + ";");
      }
      System.Console.WriteLine("\n");


      foreach (Type t in typeList)
      {
        dump_type(t, 0);
      }
    }
  }
}
