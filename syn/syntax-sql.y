{
  var SQL = {
    listToString: function(x, xs) {
      return [x].concat(xs).join("");
    }
  };
}

Start
  = Stmt

Stmt
  = SelectStmt

/* Statements */

SelectStmt
  = _ SelectToken
    _ x:SelectField xs:SelectFieldRest*
    _ FromToken
    _ from:Identifier
    _ where:WhereExpr? {
    return {
      type: "SELECT",
      fields: [x].concat(xs),
      from,
      where
    };
  }

SelectField "select valid field"
  = Identifier
  / "*"

SelectFieldRest
  = _ SeparatorToken _ s:SelectField {
    return s;
  }

WhereExpr "where expression"
  = WhereToken x:LogicExpr xs:LogicExprRest* {
    return {
      conditions: [x].concat(xs)
    };
  }

LogicExpr
  = _ "(" _ x:LogicExpr xs:LogicExprRest* _ ")" _ {
    return [x].concat(xs);
  }
  / _ left:Expr _ op:Operator _ right:Expr _ {
    return { left, op, right };
  }

LogicExprRest
  = _ j:Joiner _ l:LogicExpr {
    return {
      joiner: j,
      expression: l
    };
  }

Joiner "joiner"
  = OrToken  { return "Or";  }
  / AndToken { return "And"; }

Operator
  = "<>"       { return "Different"; }
  / "="        { return "Equal";     }
  / LikeToken  { return "Like";      }

/* Expressions */

Expr
  = Float
  / Integer
  / Identifier
  / String

Integer "integer"
  = n:[0-9]+ {
    return parseInt(n.join(""));
  }

Float "float"
  = left:Integer "." right:Integer {
    return parseFloat([
      left.toString(),
      right.toString()
    ].join("."));
  }

String "string"
  = "'" str:ValidStringChar* "'" {
    return str.join("");
  }

ValidStringChar
  = !"'" c:. {
    return c;
  }

/* Tokens */

SelectToken
  = "SELECT"i !IdentRest

SeparatorToken
  = ","

FromToken
  = "FROM"i !IdentRest

WhereToken
  = "WHERE"i !IdentRest

LikeToken
  = "LIKE"i !IdentRest

OrToken
  = "OR"i !IdentRest

AndToken
  = "AND"i !IdentRest

/* Identifier */

Identifier "identifier"
  = x:IdentStart xs:IdentRest* {
    return SQL.listToString(x, xs);
  }

IdentStart
  = [a-z_]i

IdentRest
  = [a-z0-9_]i

/* Skip */
_
  = ( WhiteSpace / NewLine )*

NewLine "newline"
  = "\r\n"
  / "\r"
  / "\n"
  / "\u2028"
  / "\u2029"

WhiteSpace "whitespace"
  = " "
  / "\t"
  / "\v"
  / "\f"