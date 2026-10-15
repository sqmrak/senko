#ifndef SENKO_ROUTING_UI_H
#define SENKO_ROUTING_UI_H

#import <UIKit/UIKit.h>

/* the muted caption above or below a table section, wrapped to the table's
   width. the routing screens share it, and each measures it for its own rows */
CGFloat SenkoCaptionHeight(NSString *text, UIFont *font, UITableView *tv);
UIView *SenkoCaptionView(NSString *text, UIFont *font, UITableView *tv);

/* the color an action wears everywhere it is shown: the tunnel, direct, block */
UIColor *SenkoActionColor(NSString *action);
NSString *SenkoActionTitle(NSString *action);

#endif
